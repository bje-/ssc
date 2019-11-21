/**
BSD-3-Clause
Copyright 2019 Alliance for Sustainable Energy, LLC
Redistribution and use in source and binary forms, with or without modification, are permitted provided 
that the following conditions are met :
1.	Redistributions of source code must retain the above copyright notice, this list of conditions 
and the following disclaimer.
2.	Redistributions in binary form must reproduce the above copyright notice, this list of conditions 
and the following disclaimer in the documentation and/or other materials provided with the distribution.
3.	Neither the name of the copyright holder nor the names of its contributors may be used to endorse 
or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, 
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
ARE DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDER, CONTRIBUTORS, UNITED STATES GOVERNMENT OR UNITED STATES 
DEPARTMENT OF ENERGY, NOR ANY OF THEIR EMPLOYEES, BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, 
OR CONSEQUENTIAL DAMAGES(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; 
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT 
OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <math.h>

#include "common.h"
#include "core.h"
#include "lib_util.h"
#include "lib_shared_inverter.h"
#include "lib_battery.h"
#include "cmod_battery.h"
#include "lib_power_electronics.h"
#include "lib_resilience.h"

#include "cmod_battwatts.h"


var_info vtab_battwatts[] = {
	/*   VARTYPE           DATATYPE         NAME                               LABEL                                    UNITS      META                   GROUP                  REQUIRED_IF                 CONSTRAINTS                      UI_HINTS*/
	{ SSC_INPUT,        SSC_NUMBER,      "system_use_lifetime_output",        "PV lifetime simulation",                 "0/1",     "0=SingleYearRepeated,1=RunEveryYear",                     "Lifetime",             "?=0",                        "BOOLEAN",                        "" },
	{ SSC_INPUT,        SSC_NUMBER,      "analysis_period",                   "Lifetime analysis period",               "years",   "The number of years in the simulation",                   "Lifetime",             "system_use_lifetime_output=1",   "",                               "" },
	{ SSC_INPUT,        SSC_NUMBER,      "batt_simple_enable",                "Enable Battery",                         "0/1",     "",                 "Battery",                  "?=0",                        "BOOLEAN",                       "" },
	{ SSC_INPUT,        SSC_NUMBER,      "batt_simple_kwh",                   "Battery Capacity",                       "kWh",     "",                 "Battery",                  "?=0",                        "",                              "" },
	{ SSC_INPUT,        SSC_NUMBER,      "batt_simple_kw",                    "Battery Power",                          "kW",      "",                 "Battery",                  "?=0",                        "",                              "" },
	{ SSC_INPUT,        SSC_NUMBER,      "batt_simple_chemistry",             "Battery Chemistry",                      "0=LeadAcid,1=Li-ion/2",   "",                 "Battery",                  "?=0",                        "",                              "" },
    { SSC_INPUT,        SSC_NUMBER,      "batt_simple_dispatch",              "Battery Dispatch",                       "0=PeakShavingLookAhead,1=PeakShavingLookBehind,2=Custom",     "",                 "Battery",                  "?=0",                        "",                              "" },
    { SSC_INPUT,        SSC_ARRAY,       "batt_custom_dispatch",              "Battery Dispatch",                       "kW",      "",                 "Battery",                  "batt_simple_dispatch=2",                        "",                              "" },
	{ SSC_INPUT,        SSC_NUMBER,      "batt_simple_meter_position",        "Battery Meter Position",                 "0=BehindTheMeter,1=FrontOfMeter",     "",                 "Battery",                  "?=0",                        "",                              "" },
	{ SSC_INPUT,        SSC_ARRAY,       "dc",						         "DC array power",                         "W",       "",                 "Battery",                           "",                           "",                              "" },
	{ SSC_INPUT,        SSC_ARRAY,       "ac",							     "AC inverter power",                      "W",       "",                 "Battery",                           "",                           "",                              "" },
    { SSC_INPUT,		SSC_ARRAY,	     "load",			                     "Electricity load (year 1)",              "kW",	   "",		           "Battery",                           "",	                         "",	                          "" },
    { SSC_INPUT,		SSC_ARRAY,	     "crit_load",			             "Critical electricity load (year 1)",     "kW",	   "",		           "Battery",                           "",	                         "",	                          "" },
	{ SSC_INPUT,        SSC_NUMBER,      "inverter_model",                    "Inverter model specifier",                 "",      "0=cec,1=datasheet,2=partload,3=coefficientgenerator,4=generic", "Battery",     "",                           "INTEGER,MIN=0,MAX=4",           "" },
	{ SSC_INPUT,        SSC_NUMBER,      "inverter_efficiency",               "Inverter Efficiency",                     "%",      "",                  "Battery",                          "",                           "MIN=0,MAX=100",                               "" },

var_info_invalid  };

std::shared_ptr<batt_variables>
battwatts_create(size_t n_recs, int chem, int meter_pos, double size_kwh, double size_kw, double inv_eff,
                 int dispatch, std::vector<double> dispatch_custom){
    auto batt_vars = std::make_shared<batt_variables>();

    // allocate vectors
    auto lifetime_matrix = new std::vector < double >;
    auto capacity_vs_temperature = new std::vector<double>;
    double batt_specific_energy_per_mass = 0;
    double batt_specific_energy_per_volume = 0;

    // Basic information
    batt_vars->batt_chem = chem;
    batt_vars->analysis_period = 25;
    batt_vars->batt_meter_position = meter_pos;
    batt_vars->system_use_lifetime_output = false;
    double voltage_guess = 0;

    // lithium ion NMC
    if (batt_vars->batt_chem == battery_t::LITHIUM_ION)
    {
        // Voltage properties
        voltage_guess = 500;
        batt_vars->batt_Vnom_default = 3.6;
        batt_vars->batt_Vfull = 4.1;
        batt_vars->batt_Vexp = 4.05;
        batt_vars->batt_Vnom = 3.4;
        batt_vars->batt_Qfull = 2.25;
        batt_vars->batt_Qfull_flow = 0;
        batt_vars->batt_Qexp = 0.178 * batt_vars->batt_Qfull;
        batt_vars->batt_Qnom = 0.889 * batt_vars->batt_Qfull;
        batt_vars->batt_C_rate = 0.2;
        batt_vars->batt_resistance = 0.1;

        // Battery lifetime
        lifetime_matrix->push_back(20); lifetime_matrix->push_back(0); lifetime_matrix->push_back(100);
        lifetime_matrix->push_back(20); lifetime_matrix->push_back(5000); lifetime_matrix->push_back(80);
        lifetime_matrix->push_back(80); lifetime_matrix->push_back(0); lifetime_matrix->push_back(100);
        lifetime_matrix->push_back(80); lifetime_matrix->push_back(1000); lifetime_matrix->push_back(80);
        util::matrix_t<double> batt_lifetime_matrix(4, 3, lifetime_matrix);
        batt_vars->batt_lifetime_matrix = batt_lifetime_matrix;

        batt_vars->batt_calendar_q0 = 1.02;
        batt_vars->batt_calendar_a = 2.66e-3;
        batt_vars->batt_calendar_b = -7280;
        batt_vars->batt_calendar_c = 930;

        // Thermal behavior
        capacity_vs_temperature->push_back(-15); capacity_vs_temperature->push_back(65);
        capacity_vs_temperature->push_back(0);  capacity_vs_temperature->push_back(85);
        capacity_vs_temperature->push_back(25); capacity_vs_temperature->push_back(100);
        capacity_vs_temperature->push_back(40); capacity_vs_temperature->push_back(104);
        util::matrix_t<double> batt_capacity_vs_temperature(4, 2, capacity_vs_temperature);
        batt_vars->cap_vs_temp = batt_capacity_vs_temperature;
        batt_vars->batt_Cp = 1004;
        batt_vars->batt_h_to_ambient = 500;
        for (size_t i = 0; i < n_recs; i++) {
            batt_vars->T_room.push_back(293);
        }
        batt_specific_energy_per_mass = 197.33;  // Wh/kg
        batt_specific_energy_per_volume = 501.25; // Wh/L
    }
        // Lead acid AGM defaults
    else if (batt_vars->batt_chem == battery_t::LEAD_ACID)
    {
        // Capacity properties
        double LeadAcid_q20 = 100;
        double LeadAcid_q10 = 93.2;
        double LeadAcid_qn = 58.12;
        double LeadAcid_tn = 1;

        batt_vars->LeadAcid_q10_computed = batt_vars->batt_computed_strings * LeadAcid_q10 * batt_vars->batt_Qfull / 100;
        batt_vars->LeadAcid_q20_computed = batt_vars->batt_computed_strings * LeadAcid_q20 * batt_vars->batt_Qfull / 100;
        batt_vars->LeadAcid_qn_computed = batt_vars->batt_computed_strings * LeadAcid_qn * batt_vars->batt_Qfull / 100;
        batt_vars->LeadAcid_tn = LeadAcid_tn;

        // Voltage properties
        voltage_guess = 48;
        batt_vars->batt_Vnom_default = 2;
        batt_vars->batt_Vfull = 2.2;
        batt_vars->batt_Vexp = 2.06;
        batt_vars->batt_Vnom = 2.03;
        batt_vars->batt_Qfull = 20;
        batt_vars->batt_Qexp = 0.025 * batt_vars->batt_Qfull;
        batt_vars->batt_Qnom = 0.90 * batt_vars->batt_Qfull;
        batt_vars->batt_C_rate = 0.05;
        batt_vars->batt_resistance = 0.1;

        // Battery lifetime
        lifetime_matrix->push_back(30); lifetime_matrix->push_back(0); lifetime_matrix->push_back(100);
        lifetime_matrix->push_back(30); lifetime_matrix->push_back(1100); lifetime_matrix->push_back(90);
        lifetime_matrix->push_back(30); lifetime_matrix->push_back(1200); lifetime_matrix->push_back(50);
        lifetime_matrix->push_back(50); lifetime_matrix->push_back(0); lifetime_matrix->push_back(100);
        lifetime_matrix->push_back(50); lifetime_matrix->push_back(400); lifetime_matrix->push_back(90);
        lifetime_matrix->push_back(50); lifetime_matrix->push_back(500); lifetime_matrix->push_back(50);
        lifetime_matrix->push_back(100); lifetime_matrix->push_back(0); lifetime_matrix->push_back(100);
        lifetime_matrix->push_back(100); lifetime_matrix->push_back(100); lifetime_matrix->push_back(90);
        lifetime_matrix->push_back(100); lifetime_matrix->push_back(150); lifetime_matrix->push_back(50);
        util::matrix_t<double> batt_lifetime_matrix(9, 3, lifetime_matrix);
        batt_vars->batt_lifetime_matrix = batt_lifetime_matrix;

        // Thermal behavior
        capacity_vs_temperature->push_back(-15); capacity_vs_temperature->push_back(65);
        capacity_vs_temperature->push_back(0);  capacity_vs_temperature->push_back(85);
        capacity_vs_temperature->push_back(25); capacity_vs_temperature->push_back(100);
        capacity_vs_temperature->push_back(40); capacity_vs_temperature->push_back(104);
        util::matrix_t<double> batt_capacity_vs_temperature(4, 2, capacity_vs_temperature);
        batt_vars->cap_vs_temp = batt_capacity_vs_temperature;

        batt_vars->batt_Cp = 600;
        batt_vars->batt_h_to_ambient = 500;
        for (size_t i = 0; i < n_recs; i++) {
            batt_vars->T_room.push_back(293);
        }
        batt_specific_energy_per_mass = 30;  // Wh/kg
        batt_specific_energy_per_volume = 30; // Wh/L
    }

    batt_vars->batt_kwh = size_kwh;
    batt_vars->batt_kw = size_kw;
    batt_vars->batt_computed_series = (int)std::ceil(voltage_guess / batt_vars->batt_Vnom_default);
    batt_vars->batt_computed_strings = (int)std::ceil((batt_vars->batt_kwh * 1000.) / (batt_vars->batt_Qfull * batt_vars->batt_computed_series * batt_vars->batt_Vnom_default)) - 1;
    batt_vars->batt_kwh = batt_vars->batt_computed_strings * batt_vars->batt_Qfull * batt_vars->batt_computed_series * batt_vars->batt_Vnom_default / 1000.;

    // Common Voltage properties
    batt_vars->batt_voltage_choice = voltage_t::VOLTAGE_MODEL;
    batt_vars->batt_voltage_matrix = util::matrix_t<double>();

    // Current and Capacity
    double batt_time_hour = batt_vars->batt_kwh / batt_vars->batt_kw;
    double batt_C_rate_discharge = 1. / batt_time_hour;
    batt_vars->batt_current_choice = dispatch_t::RESTRICT_CURRENT;
    batt_vars->batt_current_charge_max = 1000 * batt_C_rate_discharge * batt_vars->batt_kwh / voltage_guess;
    batt_vars->batt_current_discharge_max = 1000 * batt_C_rate_discharge * batt_vars->batt_kwh / voltage_guess;
    batt_vars->batt_power_charge_max_kwac = batt_vars->batt_kw;
    batt_vars->batt_power_discharge_max_kwac = batt_vars->batt_kw;
    batt_vars->batt_power_charge_max_kwdc = batt_vars->batt_kw / (batt_vars->batt_dc_ac_efficiency * 0.01);
    batt_vars->batt_power_discharge_max_kwdc = batt_vars->batt_kw / (batt_vars->batt_ac_dc_efficiency * 0.01);

    // Power converters and topology
    batt_vars->batt_topology = ChargeController::AC_CONNECTED;
    batt_vars->batt_ac_dc_efficiency = 96;
    batt_vars->batt_dc_ac_efficiency = 96;
    batt_vars->batt_dc_dc_bms_efficiency = 99;
    batt_vars->pv_dc_dc_mppt_efficiency = 99;

    // Charge limits and priority
    batt_vars->batt_initial_SOC = 50.;
    batt_vars->batt_maximum_SOC = 95.;
    batt_vars->batt_minimum_SOC = 15.;
    batt_vars->batt_minimum_modetime = 10;

    // Storage dispatch controllers
    switch (dispatch){
        default:
        case 0: batt_vars->batt_dispatch = dispatch_t::LOOK_AHEAD;
            break;
        case 1: batt_vars->batt_dispatch = dispatch_t::LOOK_BEHIND;
            break;
        case 2: batt_vars->batt_dispatch = dispatch_t::CUSTOM_DISPATCH;
            batt_vars->batt_custom_dispatch = std::move(dispatch_custom);

    }
    batt_vars->batt_dispatch_auto_can_charge = true;
    batt_vars->batt_dispatch_auto_can_gridcharge = true;

    // Battery bank replacement
    batt_vars->batt_replacement_capacity = 0.;

    // Battery lifetime
    batt_vars->batt_calendar_choice = lifetime_calendar_t::NONE;
    batt_vars->batt_calendar_lifetime_matrix = util::matrix_t<double>();
    batt_vars->batt_calendar_q0 = 1.0;

    // Common Thermal behavior
    batt_vars->batt_mass = batt_vars->batt_kwh * 1000 / batt_specific_energy_per_mass;
    double batt_volume = batt_vars->batt_kwh / batt_specific_energy_per_volume;
    batt_vars->batt_length = std::pow(batt_volume, 1. / 3.);
    batt_vars->batt_width = std::pow(batt_volume, 1. / 3.);
    batt_vars->batt_height = std::pow(batt_volume, 1. / 3.);

    // Losses
    batt_vars->batt_loss_choice = losses_t::MONTHLY;
    batt_vars->batt_losses_charging.emplace_back(0);
    batt_vars->batt_losses_discharging.emplace_back(0);
    batt_vars->batt_losses_idle.emplace_back(0);

    // Inverter model must be
    batt_vars->inverter_model = SharedInverter::NONE;
    batt_vars->inverter_efficiency = inv_eff;

    // Clean up
    delete lifetime_matrix;
    delete capacity_vs_temperature;

    return batt_vars;
}

cm_battwatts::cm_battwatts()
{
    add_var_info(vtab_battwatts);
    add_var_info(vtab_battery_outputs);
    add_var_info(vtab_technology_outputs);
    add_var_info(vtab_resilience_outputs);
}

std::shared_ptr<batt_variables> cm_battwatts::setup_variables(size_t n_recs)
{
    int chem = as_integer("batt_simple_chemistry");
    int pos = as_integer("batt_simple_meter_position");
    double kwh = as_number("batt_simple_kwh");
    double kw = as_number("batt_simple_kw");
    double inv_eff = as_number("inverter_efficiency");
    int dispatch = as_integer("batt_simple_dispatch");
    std::vector<double> dispatch_custom;
    if (dispatch == 2){
        dispatch_custom = as_vector_double("batt_custom_dispatch");
        if (dispatch_custom.size()!=n_recs) throw exec_error("battwatts",
                "'batt_custom_dispatch' length must be equal to length of 'ac'.");
    }
    return battwatts_create(n_recs, chem, pos, kwh, kw, inv_eff, dispatch, dispatch_custom);
}


void cm_battwatts::exec()
{
    if (as_boolean("batt_simple_enable"))
    {

        /* *********************************************************************************************
        Setup problem
        *********************************************************************************************** */
        std::vector<ssc_number_t> p_ac;
        std::vector<ssc_number_t> p_load;

        const double voltage = 500;

        p_ac = as_vector_ssc_number_t("ac");
        util::vector_multiply_scalar<ssc_number_t>(p_ac, static_cast<ssc_number_t>(util::watt_to_kilowatt));
        p_load = as_vector_ssc_number_t("load");

        std::shared_ptr<batt_variables> batt_vars = setup_variables(p_ac.size());
        auto batt = std::make_shared<battstor>(*m_vartab, true, p_ac.size(), static_cast<double>(8760 / p_ac.size()), batt_vars);
        batt->initialize_automated_dispatch(p_ac, p_load);

        std::unique_ptr<resiliency_runner> resilience = nullptr;
        std::vector<ssc_number_t> p_crit_load;
        if (is_assigned("crit_load")){
            p_crit_load = as_vector_ssc_number_t("crit_load");
            if (p_crit_load.size() != p_ac.size())
                throw exec_error("battwatts", "electric load profile must have same number of values as ac");
            if (!p_crit_load.empty() && *std::max_element(p_crit_load.begin(), p_crit_load.end()) > 0){
                resilience = std::unique_ptr<resiliency_runner>(new resiliency_runner(batt));
                auto logs = resilience->get_logs();
                if (!logs.empty()){
                    log(logs[0], SSC_WARNING);
                }
            }
        }

        /* *********************************************************************************************
        Run Simulation
        *********************************************************************************************** */
        ssc_number_t *p_gen = allocate("gen", p_ac.size());
        size_t hour = 0;
        int count = 0;

        for (hour = 0; hour < 8760; hour++)
        {
            for (size_t jj = 0; jj < batt->step_per_hour; jj++)
            {
                batt->initialize_time(0, hour, jj);

                if (resilience){
                    resilience->add_battery_at_outage_timestep(*batt->dispatch_model, count);
                    resilience->run_surviving_batteries(p_crit_load[count], p_ac[count]);
                }

                batt->advance(m_vartab, p_ac[count], voltage, p_load[count]);
                p_gen[count] = batt->outGenPower[count];
                count++;
            }
        }
        process_messages(batt, this);
        batt->calculate_monthly_and_annual_outputs(*this);

        if (resilience) {
            resilience->run_surviving_batteries_by_looping(&p_crit_load[0], &p_ac[0]);

            double avg_hours_survived = resilience->compute_metrics(batt->step_per_hour);
            auto outage_durations = resilience->get_outage_duration_hrs();
            auto probs_surviving = resilience->get_probs_of_surviving();
            assign("resilience_hrs", resilience->get_hours_survived());
            assign("resilience_hrs_min", (int) outage_durations[0]);
            assign("resilience_hrs_max", (int) outage_durations.back());
            assign("resilience_hrs_avg", avg_hours_survived);
            assign("outage_durations", outage_durations);
            assign("probs_of_surviving", probs_surviving);
            assign("avg_critical_load", resilience->get_avg_crit_load_kwh());
        }
    }
    else
        assign("average_battery_roundtrip_efficiency", var_data((ssc_number_t)0.));
}

DEFINE_MODULE_ENTRY(battwatts, "simple battery model", 1)
