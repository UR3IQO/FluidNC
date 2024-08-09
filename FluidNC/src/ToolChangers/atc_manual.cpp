// Copyright (c) 2024 -	Bart Dring
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "atc_manual.h"
#include "../Machine/MachineConfig.h"
#include <cstdio>
#include <iostream>

/*
  safe_z_mm: Set this to the mpos height you want the Z to travel around when tool changing. It is typically near the top so the longest tool can clear the work.
  change_mpos_mm: This is where the machine will go for the manual tool change. 

  ets_mpos_mm: The X and Y location are the XY center of the toolsetter. The Z is the lowest the Z should go before we fail due to missing bit.

  How do you tell FNC you already have a tool number installed before starting a job.

  How do you tell it you want to install a tool before a job.

  M6T0 from T<anything> -- Resets the offsets for a new job

  M6T<not 0> From T0 --  Moves to the change location and does nothing else. Assumes work zero needs to be set
  
  M6T<not 0> to T<anything> first time 
     -- Determines the TS offset
     -- Goes to toolchange location
     -- Set TLO
     -- Returns to position before command

  M6T<not 0> to T<anything> after first time
     -- Goes to toolchange location
     -- Set TLO
     -- Returns to position before command

  Posible New Persistant values (might want a save_ATC_values: config item. default false)
     -- TLO
     -- Tool number


tool_changer:
  safe_z_mpos_mm: -1.000000
  probe_seek_rate_mm_per_min: 800.000000
  probe_feed_rate_mm_per_min: 80.000000
  change_mpos_mm: 80.000 0.000 -1.000
  ets_mpos_mm: 5.000 -17.000 -40.000
*/

namespace ATCs {
    void Manual_ATC::init() {
        log_info("ATC:" << name());
    }

    void Manual_ATC::probe_notification() {}

    bool Manual_ATC::tool_change(uint8_t new_tool, bool pre_select, bool set_tool) {
        bool spindle_was_on = false;

        // M6T0 is used to reset this ATC and allow us to start a new job
        if (new_tool == 0 || set_tool) {
            _prev_tool = new_tool;
            reset();
            return true;
        }

        try {
            protocol_buffer_synchronize();  // wait for all motion to complete

            _macro._gcode = "";  // clear previous gcode

            if (_prev_tool == 0) {  // M6T<anything> from T0 is used for a manual change before zero'ing
                log_info("Load first tool");
                move_to_change_location();
                _macro.addf("G4P0 0.1");
                _macro.addf("(MSG : Install tool #1 then resume to continue)");
                _macro.run(nullptr);
                _prev_tool = new_tool;
                return true;
            }

            _prev_tool = new_tool;

            _macro.addf("#<start_x >= #<_x>");
            _macro.addf("#<start_y >= #<_y>");
            _macro.addf("#<start_z >= #<_z>");

            move_to_save_z();

            // turn off the spindle
            if (gc_state.modal.spindle != SpindleState::Disable) {
                spindle_was_on = true;
                _macro.addf("M5");
            }

            // if we have not determined the tool setter offset yet, we need to do that.
            if (!_have_tool_setter_offset) {
                log_info("Need TLO T1");
                move_over_toolsetter();
                // do a seek probe if needed
                ets_probe();

                _macro.addf("#<_ets_tool1_z>=[#5063]");
                _macro.addf("D#<_ets_tool1_z>");

                _have_tool_setter_offset = true;
            }

            move_to_change_location();

            _macro.addf("G4P0 0.1");
            _macro.addf("(MSG: Install tool #%d)", new_tool);
            _macro.addf("M0");

            // probe the new tool
            move_to_save_z();
            move_over_toolsetter();

            ets_probe();

            //log_info("Set TLO:" << tlo);
            _macro.addf("#<_my_tlo_z >=[#5063 - #<_ets_tool1_z>]");
            _macro.addf("G43.1Z#<_my_tlo_z>");

            move_to_save_z();

            _macro.addf("G0X#<start_x>Y#<start_y>");
            //_macro.addf("G0Z#<start_z>");

            if (spindle_was_on) {
                _macro.addf("M3");  // spindle should handle spinup delay
            }

            _macro.run(nullptr);

            return true;
        } catch (...) { log_info("Exception caught"); }

        return false;
    }

    void Manual_ATC::reset() {
        _is_OK                   = true;
        _have_tool_setter_offset = false;
        _prev_tool               = gc_state.tool;  // Double check this
    }

    void Manual_ATC::move_to_change_location() {
        move_to_save_z();
        _macro.addf("G53G0X%0.3fY%0.3fZ%0.3f", _change_mpos[0], _change_mpos[1], _change_mpos[2]);
    }

    void Manual_ATC::move_to_save_z() {
        _macro.addf("G53G0Z%0.3f", _safe_z);
    }

    void Manual_ATC::move_over_toolsetter() {
        move_to_save_z();
        _macro.addf("G53G0X%0.3fY%0.3f", _ets_mpos[0], _ets_mpos[1]);
    }

    void Manual_ATC::ets_probe() {
        _macro.addf("G53G0Z #</ atc_manual / ets_rapid_z_mpos_mm>");  // rapid down

        // do a fast probe if there is a seek that is faster than feed
        if (_probe_seek_rate > _probe_feed_rate) {
            _macro.addf("G53 G38.2 Z%0.3f F%0.3f", _ets_mpos[2], _probe_seek_rate);
            _macro.addf("G0Z[#<_z> + 5]");  // move up 5mm
        }

        // do the feed rate probe
        _macro.addf("G53 G38.2 Z%0.3f F%0.3f", _ets_mpos[2], _probe_feed_rate);
    }

    namespace {
        ATCFactory::InstanceBuilder<Manual_ATC> registration("atc_manual");
    }
}
