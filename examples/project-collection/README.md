# 100 ESP32 projects with ET-Core and Home Assistant

Each project pairs an existing Arduino sketch with a new Home Assistant automation blueprint. The default action posts a local persistent notification. You can choose a different action during setup.

## Build one

1. Follow the linked sketch for components, libraries, wiring and calibration. Use a Wi-Fi-capable ESP32 supported by that sketch. Set the Wi-Fi credentials and the same ET-Core key as the Home Assistant integration. Give each node a unique ET-Core device ID before running multiple copies.
2. Confirm that the selected reading or binary state changes in Home Assistant. Check the units against the project table.
3. Copy the linked YAML file into Home Assistant under `config/blueprints/automation/etcore/`. Reload automations, create an automation from the blueprint, and select the ET-Core entity. Numeric thresholds are starting points and can be edited.
4. Test by moving the reading across the threshold or changing the contact state, then keeping it there for the listed time. A zero-minute hold fires on the transition. Home Assistant restarts reset pending hold timers.

| # | Project blueprint | ET-Core sketch | Select in Home Assistant | Trigger |
|---:|---|---|---|---|
| 1 | [Greenhouse frost watch](001_greenhouse_frost_watch.yaml) | [DS18B20](../../arduino/libraries/ETCore/examples/DS18B20/DS18B20.ino) | probe temperature (°C) | below 3, 5 min |
| 2 | [Compost heating milestone](002_compost_heating_milestone.yaml) | [DS18B20](../../arduino/libraries/ETCore/examples/DS18B20/DS18B20.ino) | probe temperature (°C) | above 45, 30 min |
| 3 | [Desk enclosure heat soak](003_desk_enclosure_heat_soak.yaml) | [DS18B20](../../arduino/libraries/ETCore/examples/DS18B20/DS18B20.ino) | probe temperature (°C) | above 38, 10 min |
| 4 | [Cold frame warmed for transplanting](004_cold_frame_warmed_for_transplanting.yaml) | [DS18B20](../../arduino/libraries/ETCore/examples/DS18B20/DS18B20.ino) | probe temperature (°C) | above 16, 20 min |
| 5 | [Pond summer warmth](005_pond_summer_warmth.yaml) | [DS18B20](../../arduino/libraries/ETCore/examples/DS18B20/DS18B20.ino) | probe temperature (°C) | above 26, 30 min |
| 6 | [Brew water preheat milestone](006_brew_water_preheat_milestone.yaml) | [DS18B20](../../arduino/libraries/ETCore/examples/DS18B20/DS18B20.ino) | probe temperature (°C) | above 65, 1 min |
| 7 | [Seed mat overnight cooling](007_seed_mat_overnight_cooling.yaml) | [DS18B20](../../arduino/libraries/ETCore/examples/DS18B20/DS18B20.ino) | probe temperature (°C) | below 17, 10 min |
| 8 | [Rain barrel near freezing](008_rain_barrel_near_freezing.yaml) | [DS18B20](../../arduino/libraries/ETCore/examples/DS18B20/DS18B20.ino) | probe temperature (°C) | below 2, 10 min |
| 9 | [Workshop warmed for painting](009_workshop_warmed_for_painting.yaml) | [DS18B20](../../arduino/libraries/ETCore/examples/DS18B20/DS18B20.ino) | probe temperature (°C) | above 18, 15 min |
| 10 | [Cooling bath ready](010_cooling_bath_ready.yaml) | [DS18B20](../../arduino/libraries/ETCore/examples/DS18B20/DS18B20.ino) | probe temperature (°C) | below 10, 2 min |
| 11 | [Porch dusk reminder](011_porch_dusk_reminder.yaml) | [BH1750](../../arduino/libraries/ETCore/examples/BH1750/BH1750.ino) | illuminance (lx) | below 25, 5 min |
| 12 | [Reading desk light check](012_reading_desk_light_check.yaml) | [BH1750](../../arduino/libraries/ETCore/examples/BH1750/BH1750.ino) | illuminance (lx) | below 300, 3 min |
| 13 | [East window sunrise](013_east_window_sunrise.yaml) | [BH1750](../../arduino/libraries/ETCore/examples/BH1750/BH1750.ino) | illuminance (lx) | above 500, 5 min |
| 14 | [Projection room light spill](014_projection_room_light_spill.yaml) | [BH1750](../../arduino/libraries/ETCore/examples/BH1750/BH1750.ino) | illuminance (lx) | above 40, 1 min |
| 15 | [Display cabinet left lit](015_display_cabinet_left_lit.yaml) | [BH1750](../../arduino/libraries/ETCore/examples/BH1750/BH1750.ino) | illuminance (lx) | above 100, 90 min |
| 16 | [Grow shelf daylight reached](016_grow_shelf_daylight_reached.yaml) | [BH1750](../../arduino/libraries/ETCore/examples/BH1750/BH1750.ino) | illuminance (lx) | above 5000, 10 min |
| 17 | [Darkroom light leak check](017_darkroom_light_leak_check.yaml) | [BH1750](../../arduino/libraries/ETCore/examples/BH1750/BH1750.ino) | illuminance (lx) | above 2, 0 min |
| 18 | [Workshop task light ready](018_workshop_task_light_ready.yaml) | [BH1750](../../arduino/libraries/ETCore/examples/BH1750/BH1750.ino) | illuminance (lx) | above 750, 1 min |
| 19 | [West window glare](019_west_window_glare.yaml) | [BH1750](../../arduino/libraries/ETCore/examples/BH1750/BH1750.ino) | illuminance (lx) | above 12000, 3 min |
| 20 | [Covered sensor darkness test](020_covered_sensor_darkness_test.yaml) | [BH1750](../../arduino/libraries/ETCore/examples/BH1750/BH1750.ino) | illuminance (lx) | below 1, 1 min |
| 21 | [Bathroom steam event](021_bathroom_steam_event.yaml) | [BME280](../../arduino/libraries/ETCore/examples/BME280/BME280.ino) | relative humidity (%) | above 75, 3 min |
| 22 | [Filament dry box moisture](022_filament_dry_box_moisture.yaml) | [BME280](../../arduino/libraries/ETCore/examples/BME280/BME280.ino) | relative humidity (%) | above 35, 10 min |
| 23 | [Camera cabinet too dry](023_camera_cabinet_too_dry.yaml) | [BME280](../../arduino/libraries/ETCore/examples/BME280/BME280.ino) | relative humidity (%) | below 30, 30 min |
| 24 | [Basement damp spell](024_basement_damp_spell.yaml) | [BME280](../../arduino/libraries/ETCore/examples/BME280/BME280.ino) | relative humidity (%) | above 65, 60 min |
| 25 | [Laundry drying room recovered](025_laundry_drying_room_recovered.yaml) | [BME280](../../arduino/libraries/ETCore/examples/BME280/BME280.ino) | relative humidity (%) | below 55, 15 min |
| 26 | [Propagation dome dry air](026_propagation_dome_dry_air.yaml) | [BME280](../../arduino/libraries/ETCore/examples/BME280/BME280.ino) | relative humidity (%) | below 65, 5 min |
| 27 | [Instrument cupboard humidity dip](027_instrument_cupboard_humidity_dip.yaml) | [BME280](../../arduino/libraries/ETCore/examples/BME280/BME280.ino) | relative humidity (%) | below 40, 20 min |
| 28 | [Greenhouse overnight saturation](028_greenhouse_overnight_saturation.yaml) | [BME280](../../arduino/libraries/ETCore/examples/BME280/BME280.ino) | relative humidity (%) | above 90, 15 min |
| 29 | [Workshop dehumidifier target](029_workshop_dehumidifier_target.yaml) | [BME280](../../arduino/libraries/ETCore/examples/BME280/BME280.ino) | relative humidity (%) | below 50, 10 min |
| 30 | [Storage tote moisture ingress](030_storage_tote_moisture_ingress.yaml) | [BME280](../../arduino/libraries/ETCore/examples/BME280/BME280.ino) | relative humidity (%) | above 60, 30 min |
| 31 | [Meeting room airing reminder](031_meeting_room_airing_reminder.yaml) | [SCD40](../../arduino/libraries/ETCore/examples/SCD40/SCD40.ino) | carbon dioxide (ppm) | above 1100, 10 min |
| 32 | [Bedroom morning air recovery](032_bedroom_morning_air_recovery.yaml) | [SCD40](../../arduino/libraries/ETCore/examples/SCD40/SCD40.ino) | carbon dioxide (ppm) | below 750, 10 min |
| 33 | [Studio occupancy trend prompt](033_studio_occupancy_trend_prompt.yaml) | [SCD40](../../arduino/libraries/ETCore/examples/SCD40/SCD40.ino) | carbon dioxide (ppm) | above 900, 20 min |
| 34 | [Classroom break reminder](034_classroom_break_reminder.yaml) | [SCD40](../../arduino/libraries/ETCore/examples/SCD40/SCD40.ino) | carbon dioxide (ppm) | above 1200, 5 min |
| 35 | [Office ventilation recovery](035_office_ventilation_recovery.yaml) | [SCD40](../../arduino/libraries/ETCore/examples/SCD40/SCD40.ino) | carbon dioxide (ppm) | below 800, 5 min |
| 36 | [Workshop sustained CO2 rise](036_workshop_sustained_co2_rise.yaml) | [SCD40](../../arduino/libraries/ETCore/examples/SCD40/SCD40.ino) | carbon dioxide (ppm) | above 1300, 15 min |
| 37 | [Outdoor baseline observation](037_outdoor_baseline_observation.yaml) | [SCD40](../../arduino/libraries/ETCore/examples/SCD40/SCD40.ino) | carbon dioxide (ppm) | below 550, 10 min |
| 38 | [Rehearsal room air break](038_rehearsal_room_air_break.yaml) | [SCD40](../../arduino/libraries/ETCore/examples/SCD40/SCD40.ino) | carbon dioxide (ppm) | above 1400, 5 min |
| 39 | [Study overnight accumulation](039_study_overnight_accumulation.yaml) | [SCD40](../../arduino/libraries/ETCore/examples/SCD40/SCD40.ino) | carbon dioxide (ppm) | above 1000, 60 min |
| 40 | [Reading room fresh-air target](040_reading_room_fresh_air_target.yaml) | [SCD40](../../arduino/libraries/ETCore/examples/SCD40/SCD40.ino) | carbon dioxide (ppm) | below 700, 15 min |
| 41 | [Basil planter watering reminder](041_basil_planter_watering_reminder.yaml) | [SoilMoisture](../../arduino/libraries/ETCore/examples/SoilMoisture/SoilMoisture.ino) | calibrated soil moisture (%) | below 35, 10 min |
| 42 | [Succulent pot dry milestone](042_succulent_pot_dry_milestone.yaml) | [SoilMoisture](../../arduino/libraries/ETCore/examples/SoilMoisture/SoilMoisture.ino) | calibrated soil moisture (%) | below 15, 60 min |
| 43 | [Seed tray drying](043_seed_tray_drying.yaml) | [SoilMoisture](../../arduino/libraries/ETCore/examples/SoilMoisture/SoilMoisture.ino) | calibrated soil moisture (%) | below 45, 5 min |
| 44 | [Hanging basket irrigation reached](044_hanging_basket_irrigation_reached.yaml) | [SoilMoisture](../../arduino/libraries/ETCore/examples/SoilMoisture/SoilMoisture.ino) | calibrated soil moisture (%) | above 60, 2 min |
| 45 | [Fern planter sustained dryness](045_fern_planter_sustained_dryness.yaml) | [SoilMoisture](../../arduino/libraries/ETCore/examples/SoilMoisture/SoilMoisture.ino) | calibrated soil moisture (%) | below 40, 20 min |
| 46 | [Raised bed rain penetration](046_raised_bed_rain_penetration.yaml) | [SoilMoisture](../../arduino/libraries/ETCore/examples/SoilMoisture/SoilMoisture.ino) | calibrated soil moisture (%) | above 65, 5 min |
| 47 | [Window box slow drainage](047_window_box_slow_drainage.yaml) | [SoilMoisture](../../arduino/libraries/ETCore/examples/SoilMoisture/SoilMoisture.ino) | calibrated soil moisture (%) | above 85, 120 min |
| 48 | [Mulched bed moisture reserve](048_mulched_bed_moisture_reserve.yaml) | [SoilMoisture](../../arduino/libraries/ETCore/examples/SoilMoisture/SoilMoisture.ino) | calibrated soil moisture (%) | below 30, 30 min |
| 49 | [Drip emitter wetting test](049_drip_emitter_wetting_test.yaml) | [SoilMoisture](../../arduino/libraries/ETCore/examples/SoilMoisture/SoilMoisture.ino) | calibrated soil moisture (%) | above 55, 1 min |
| 50 | [Indoor pot prolonged saturation](050_indoor_pot_prolonged_saturation.yaml) | [SoilMoisture](../../arduino/libraries/ETCore/examples/SoilMoisture/SoilMoisture.ino) | calibrated soil moisture (%) | above 90, 180 min |
| 51 | [Rain barrel refill milestone](051_rain_barrel_refill_milestone.yaml) | [TankLevel](../../arduino/libraries/ETCore/examples/TankLevel/TankLevel.ino) | calibrated level (%) | above 75, 2 min |
| 52 | [Header tank low reserve](052_header_tank_low_reserve.yaml) | [TankLevel](../../arduino/libraries/ETCore/examples/TankLevel/TankLevel.ino) | calibrated level (%) | below 20, 1 min |
| 53 | [Hydroponic reservoir top-up](053_hydroponic_reservoir_top_up.yaml) | [TankLevel](../../arduino/libraries/ETCore/examples/TankLevel/TankLevel.ino) | calibrated level (%) | below 35, 3 min |
| 54 | [Greywater collection nearing full](054_greywater_collection_nearing_full.yaml) | [TankLevel](../../arduino/libraries/ETCore/examples/TankLevel/TankLevel.ino) | calibrated level (%) | above 85, 1 min |
| 55 | [Irrigation tote emptying](055_irrigation_tote_emptying.yaml) | [TankLevel](../../arduino/libraries/ETCore/examples/TankLevel/TankLevel.ino) | calibrated level (%) | below 10, 2 min |
| 56 | [Pond top-up target](056_pond_top_up_target.yaml) | [TankLevel](../../arduino/libraries/ETCore/examples/TankLevel/TankLevel.ino) | calibrated level (%) | above 90, 2 min |
| 57 | [Condensate bucket emptying reminder](057_condensate_bucket_emptying_reminder.yaml) | [TankLevel](../../arduino/libraries/ETCore/examples/TankLevel/TankLevel.ino) | calibrated level (%) | above 70, 2 min |
| 58 | [Water butt half-full milestone](058_water_butt_half_full_milestone.yaml) | [TankLevel](../../arduino/libraries/ETCore/examples/TankLevel/TankLevel.ino) | calibrated level (%) | above 50, 5 min |
| 59 | [Fountain reservoir service](059_fountain_reservoir_service.yaml) | [TankLevel](../../arduino/libraries/ETCore/examples/TankLevel/TankLevel.ino) | calibrated level (%) | below 25, 5 min |
| 60 | [Drain tank empty confirmation](060_drain_tank_empty_confirmation.yaml) | [TankLevel](../../arduino/libraries/ETCore/examples/TankLevel/TankLevel.ino) | calibrated level (%) | below 5, 1 min |
| 61 | [Garage side door left open](061_garage_side_door_left_open.yaml) | [DoorContact](../../arduino/libraries/ETCore/examples/DoorContact/DoorContact.ino) | contact | on, 10 min |
| 62 | [Mailbox delivery flap opened](062_mailbox_delivery_flap_opened.yaml) | [DoorContact](../../arduino/libraries/ETCore/examples/DoorContact/DoorContact.ino) | contact | on, 0 min |
| 63 | [Tool cabinet secured](063_tool_cabinet_secured.yaml) | [DoorContact](../../arduino/libraries/ETCore/examples/DoorContact/DoorContact.ino) | contact | off, 1 min |
| 64 | [Freezer door ajar](064_freezer_door_ajar.yaml) | [DoorContact](../../arduino/libraries/ETCore/examples/DoorContact/DoorContact.ino) | contact | on, 2 min |
| 65 | [Greenhouse vent closed](065_greenhouse_vent_closed.yaml) | [DoorContact](../../arduino/libraries/ETCore/examples/DoorContact/DoorContact.ino) | contact | off, 5 min |
| 66 | [Loft hatch open reminder](066_loft_hatch_open_reminder.yaml) | [DoorContact](../../arduino/libraries/ETCore/examples/DoorContact/DoorContact.ino) | contact | on, 15 min |
| 67 | [Parcel box collection lid opened](067_parcel_box_collection_lid_opened.yaml) | [DoorContact](../../arduino/libraries/ETCore/examples/DoorContact/DoorContact.ino) | contact | on, 0 min |
| 68 | [Garden gate returned shut](068_garden_gate_returned_shut.yaml) | [DoorContact](../../arduino/libraries/ETCore/examples/DoorContact/DoorContact.ino) | contact | off, 1 min |
| 69 | [Storage drawer left out](069_storage_drawer_left_out.yaml) | [DoorContact](../../arduino/libraries/ETCore/examples/DoorContact/DoorContact.ino) | contact | on, 5 min |
| 70 | [Printer enclosure door closed](070_printer_enclosure_door_closed.yaml) | [DoorContact](../../arduino/libraries/ETCore/examples/DoorContact/DoorContact.ino) | contact | off, 1 min |
| 71 | [Hallway movement notice](071_hallway_movement_notice.yaml) | [MotionPIR](../../arduino/libraries/ETCore/examples/MotionPIR/MotionPIR.ino) | motion | on, 0 min |
| 72 | [Workshop quiet interval](072_workshop_quiet_interval.yaml) | [MotionPIR](../../arduino/libraries/ETCore/examples/MotionPIR/MotionPIR.ino) | motion | off, 20 min |
| 73 | [Porch approach notice](073_porch_approach_notice.yaml) | [MotionPIR](../../arduino/libraries/ETCore/examples/MotionPIR/MotionPIR.ino) | motion | on, 0 min |
| 74 | [Laundry room idle reminder](074_laundry_room_idle_reminder.yaml) | [MotionPIR](../../arduino/libraries/ETCore/examples/MotionPIR/MotionPIR.ino) | motion | off, 10 min |
| 75 | [Shed entry log prompt](075_shed_entry_log_prompt.yaml) | [MotionPIR](../../arduino/libraries/ETCore/examples/MotionPIR/MotionPIR.ino) | motion | on, 0 min |
| 76 | [Studio break opportunity](076_studio_break_opportunity.yaml) | [MotionPIR](../../arduino/libraries/ETCore/examples/MotionPIR/MotionPIR.ino) | motion | off, 30 min |
| 77 | [Stair landing movement cue](077_stair_landing_movement_cue.yaml) | [MotionPIR](../../arduino/libraries/ETCore/examples/MotionPIR/MotionPIR.ino) | motion | on, 0 min |
| 78 | [Pantry inactivity reminder](078_pantry_inactivity_reminder.yaml) | [MotionPIR](../../arduino/libraries/ETCore/examples/MotionPIR/MotionPIR.ino) | motion | off, 5 min |
| 79 | [Garage work area quiet](079_garage_work_area_quiet.yaml) | [MotionPIR](../../arduino/libraries/ETCore/examples/MotionPIR/MotionPIR.ino) | motion | off, 45 min |
| 80 | [Utility cupboard access cue](080_utility_cupboard_access_cue.yaml) | [MotionPIR](../../arduino/libraries/ETCore/examples/MotionPIR/MotionPIR.ino) | motion | on, 0 min |
| 81 | [Washer tray wet](081_washer_tray_wet.yaml) | [WaterLeak](../../arduino/libraries/ETCore/examples/WaterLeak/WaterLeak.ino) | wet contact | on, 0 min |
| 82 | [Sink cabinet dry recovery](082_sink_cabinet_dry_recovery.yaml) | [WaterLeak](../../arduino/libraries/ETCore/examples/WaterLeak/WaterLeak.ino) | wet contact | off, 10 min |
| 83 | [Aquarium drip tray wet](083_aquarium_drip_tray_wet.yaml) | [WaterLeak](../../arduino/libraries/ETCore/examples/WaterLeak/WaterLeak.ino) | wet contact | on, 0 min |
| 84 | [Dehumidifier tray dry again](084_dehumidifier_tray_dry_again.yaml) | [WaterLeak](../../arduino/libraries/ETCore/examples/WaterLeak/WaterLeak.ino) | wet contact | off, 5 min |
| 85 | [Plant saucer persistent pooling](085_plant_saucer_persistent_pooling.yaml) | [WaterLeak](../../arduino/libraries/ETCore/examples/WaterLeak/WaterLeak.ino) | wet contact | on, 15 min |
| 86 | [Window sill rain ingress](086_window_sill_rain_ingress.yaml) | [WaterLeak](../../arduino/libraries/ETCore/examples/WaterLeak/WaterLeak.ino) | wet contact | on, 1 min |
| 87 | [Dishwasher tray dry confirmation](087_dishwasher_tray_dry_confirmation.yaml) | [WaterLeak](../../arduino/libraries/ETCore/examples/WaterLeak/WaterLeak.ino) | wet contact | off, 20 min |
| 88 | [Reservoir overflow tray wet](088_reservoir_overflow_tray_wet.yaml) | [WaterLeak](../../arduino/libraries/ETCore/examples/WaterLeak/WaterLeak.ino) | wet contact | on, 0 min |
| 89 | [Boot tray drying complete](089_boot_tray_drying_complete.yaml) | [WaterLeak](../../arduino/libraries/ETCore/examples/WaterLeak/WaterLeak.ino) | wet contact | off, 30 min |
| 90 | [Greenhouse bench runoff](090_greenhouse_bench_runoff.yaml) | [WaterLeak](../../arduino/libraries/ETCore/examples/WaterLeak/WaterLeak.ino) | wet contact | on, 2 min |
| 91 | [USB lamp switched on](091_usb_lamp_switched_on.yaml) | [INA219](../../arduino/libraries/ETCore/examples/INA219/INA219.ino) | DC load power (W) | above 1, 0 min |
| 92 | [Bench fan stopped drawing power](092_bench_fan_stopped_drawing_power.yaml) | [INA219](../../arduino/libraries/ETCore/examples/INA219/INA219.ino) | DC load power (W) | below 0.5, 1 min |
| 93 | [LED strip bright-load milestone](093_led_strip_bright_load_milestone.yaml) | [INA219](../../arduino/libraries/ETCore/examples/INA219/INA219.ino) | DC load power (W) | above 5, 1 min |
| 94 | [Sensor node sleep observation](094_sensor_node_sleep_observation.yaml) | [INA219](../../arduino/libraries/ETCore/examples/INA219/INA219.ino) | DC load power (W) | below 0.1, 2 min |
| 95 | [Desktop pump sustained run](095_desktop_pump_sustained_run.yaml) | [INA219](../../arduino/libraries/ETCore/examples/INA219/INA219.ino) | DC load power (W) | above 2, 10 min |
| 96 | [Model railway idle power](096_model_railway_idle_power.yaml) | [INA219](../../arduino/libraries/ETCore/examples/INA219/INA219.ino) | DC load power (W) | below 1, 3 min |
| 97 | [Solar test load producing power](097_solar_test_load_producing_power.yaml) | [INA219](../../arduino/libraries/ETCore/examples/INA219/INA219.ino) | DC load power (W) | above 3, 5 min |
| 98 | [Portable monitor standby](098_portable_monitor_standby.yaml) | [INA219](../../arduino/libraries/ETCore/examples/INA219/INA219.ino) | DC load power (W) | below 2, 5 min |
| 99 | [Workbench load sustained high](099_workbench_load_sustained_high.yaml) | [INA219](../../arduino/libraries/ETCore/examples/INA219/INA219.ino) | DC load power (W) | above 10, 10 min |
| 100 | [DC test fixture idle confirmation](100_dc_test_fixture_idle_confirmation.yaml) | [INA219](../../arduino/libraries/ETCore/examples/INA219/INA219.ino) | DC load power (W) | below 0.25, 2 min |
