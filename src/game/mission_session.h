// A mission in play with every station: the `MissionScreen` with its
// control-room stations, plus the 3D view adapter and services of the
// outside and attack stations (bridge, periscope, TBT, deck gun, TDC,
// radar) installed on its attack host. It keeps the detail options of the
// view in step with GDETAIL.SAV and saves GOPTION.SAV / GDETAIL.SAV when
// the command bar menus change them.
//
// Usage (the caller feeds the input into `setup.gui` and shows the frames):
//   MissionSession session(setup, Mission::load(...), saves);
//   while (session.screen().frame(ticks)) {
//     present(session.screen().screen(), session.screen().dac());
//   }
//   std::int32_t end_code = session.screen().finish();
#pragma once

#include <memory>

#include "game/mission.h"
#include "game/mission_screen.h"
#include "game/saves_options.h"
#include "game/stations/attack_services.h"
#include "game/world_view_adapter.h"

namespace sh {

// Returns the 3D view options of the detail level of GDETAIL.SAV.
WorldViewOptions world_view_options(const DetailOptions& detail);

class MissionSession {
 public:
  // Builds the screen of `mission` from `setup` and installs all the
  // stations; `saves` receives the option files and must outlive the
  // session, as must everything `setup` refers to. Throws `DataError`
  // when a station file is missing.
  MissionSession(MissionScreenSetup setup, std::unique_ptr<Mission> mission, const SaveDir& saves);
  // Removes the attack stations before the services they use.
  ~MissionSession();
  MissionSession(const MissionSession&) = delete;
  MissionSession& operator=(const MissionSession&) = delete;

  MissionScreen& screen() { return *screen_; }
  WorldViewAdapter& world() { return *world_; }
  AttackServices& services() { return *services_; }

 private:
  DetailOptions& detail_;
  std::unique_ptr<MissionScreen> screen_;
  std::unique_ptr<WorldViewAdapter> world_;
  std::unique_ptr<AttackServices> services_;
};

}  // namespace sh
