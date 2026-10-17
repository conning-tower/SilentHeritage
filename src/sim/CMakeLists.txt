sh_add_module(sh_sim
  SOURCES
    clock.cpp
    dive.cpp
    geometry.cpp
    motion.cpp
    player_sub.cpp
    ship.cpp
    simulation.cpp
    # Mission integration.
    terrain.cpp
    # Sensors and ship AI.
    ai_ship.cpp
    contacts.cpp
    intercept.cpp
    line_of_sight.cpp
    radar.cpp
    fathometer.cpp
    radar_emissions.cpp
    sensors.cpp
    sighting.cpp
    sonar.cpp
    # Weapons and damage.
    ballistic.cpp
    collision.cpp
    deck_gun.cpp
    depth_charge.cpp
    effects.cpp
    player_combat.cpp
    player_damage.cpp
    shell.cpp
    ship_combat.cpp
    ship_weapons.cpp
    tdc.cpp
    torpedo.cpp
    torpedo_tubes.cpp
    # Sky, light and weather.
    astronomy.cpp
    sky.cpp
    weather.cpp
    # Aircraft and enemy submarines.
    aircraft.cpp
    aircraft_strike.cpp
    air_sorties.cpp
    enemy_sub.cpp
    # Mission saves: the state-file building blocks.
    state_io.cpp
  DEPS sh_core sh_assets)
