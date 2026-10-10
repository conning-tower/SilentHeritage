#include "game/resume.h"

#include <algorithm>
#include <format>
#include <string>

#include "core/byte_reader.h"
#include "core/error.h"
#include "game/career_save.h"
#include "game/patrol.h"
#include "game/saves_options.h"

namespace sh {

namespace {

constexpr std::string_view kMagic = "SHRESUME";
constexpr std::uint8_t kVersion = 1;
constexpr std::string_view kContext = "RESUME.SAV";

void put_u8(std::vector<std::uint8_t>& out, std::uint8_t v) { out.push_back(v); }

void put_u16(std::vector<std::uint8_t>& out, std::uint16_t v) {
  out.push_back(static_cast<std::uint8_t>(v));
  out.push_back(static_cast<std::uint8_t>(v >> 8));
}

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  for (int shift = 0; shift < 32; shift += 8) {
    out.push_back(static_cast<std::uint8_t>(v >> shift));
  }
}

// Appends a u32 length and `bytes`.
void put_block(std::vector<std::uint8_t>& out, std::span<const std::uint8_t> bytes) {
  put_u32(out, static_cast<std::uint32_t>(bytes.size()));
  out.insert(out.end(), bytes.begin(), bytes.end());
}

// The 32-bit FNV-1a hash.
std::uint32_t fnv1a(std::span<const std::uint8_t> bytes) {
  std::uint32_t h = 2166136261u;
  for (const std::uint8_t b : bytes) {
    h = (h ^ b) * 16777619u;
  }
  return h;
}

std::span<const std::uint8_t> read_block(ByteReader& r) {
  const std::uint32_t size = r.u32();
  return r.bytes(size);
}

}  // namespace

std::vector<std::uint8_t> ResumeSnapshot::serialize() const {
  std::vector<std::uint8_t> out(kMagic.begin(), kMagic.end());
  put_u8(out, kVersion);
  put_u8(out, static_cast<std::uint8_t>(kind));
  put_u8(out, static_cast<std::uint8_t>(location.place));
  put_u32(out, static_cast<std::uint32_t>(location.roster_slot));
  put_u32(out, random);
  switch (kind) {
    case ResumeKind::kFrontEnd:
      break;
    case ResumeKind::kMission: {
      put_u32(out, static_cast<std::uint32_t>(slot.number));
      put_u16(out, static_cast<std::uint16_t>(slot.name.size()));
      out.insert(out.end(), slot.name.begin(), slot.name.end());
      put_block(out, mission.value().serialize());
      break;
    }
    case ResumeKind::kPatrol:
      put_block(out, serialize_roster(career.value()));
      break;
  }
  put_u32(out, fnv1a(out));
  return out;
}

ResumeSnapshot ResumeSnapshot::parse(std::span<const std::uint8_t> data) {
  if (data.size() < kMagic.size() + 4 ||
      !std::equal(kMagic.begin(), kMagic.end(), data.begin(), [](char a, std::uint8_t b) {
        return static_cast<std::uint8_t>(a) == b;
      })) {
    throw DataError("RESUME.SAV: not a resume snapshot");
  }
  const std::span<const std::uint8_t> body = data.first(data.size() - 4);
  ByteReader tail(data.last(4), kContext);
  if (tail.u32() != fnv1a(body)) {
    throw DataError("RESUME.SAV: damaged (bad hash)");
  }
  ByteReader r(body, kContext);
  r.skip(kMagic.size());
  if (const std::uint8_t version = r.u8(); version != kVersion) {
    throw DataError(std::format("RESUME.SAV: version {}", version));
  }
  ResumeSnapshot s;
  const std::uint8_t kind = r.u8();
  const std::uint8_t place = r.u8();
  if (kind < static_cast<std::uint8_t>(ResumeKind::kFrontEnd) || kind > static_cast<std::uint8_t>(ResumeKind::kPatrol) ||
      place > static_cast<std::uint8_t>(FrontEndPlace::kCareerBase)) {
    throw DataError("RESUME.SAV: unknown kind or place");
  }
  s.kind = static_cast<ResumeKind>(kind);
  s.location.place = static_cast<FrontEndPlace>(place);
  s.location.roster_slot = r.i32();
  s.random = r.u32();
  switch (s.kind) {
    case ResumeKind::kFrontEnd:
      break;
    case ResumeKind::kMission: {
      s.slot.number = r.i32();
      const std::uint16_t length = r.u16();
      const std::span<const std::uint8_t> name = r.bytes(length);
      s.slot.name.assign(name.begin(), name.end());
      s.mission = MissionSaveFile::parse(read_block(r));
      if (!s.mission) {
        throw DataError("RESUME.SAV: the mission is not a version 7 save");
      }
      break;
    }
    case ResumeKind::kPatrol: {
      s.career = parse_roster(read_block(r)).career;
      if (s.career->record.on_patrol == 0 || s.career->patrol_state.empty()) {
        throw DataError("RESUME.SAV: the career is not on patrol");
      }
      break;
    }
  }
  if (!r.at_end()) {
    throw DataError("RESUME.SAV: trailing bytes");
  }
  return s;
}

ResumeSnapshot mission_resume_snapshot(const Install& install, Mission& mission, const SessionStateParts& session,
                                       const SavedMissionSlot& slot, const FrontEndLocation& location) {
  ResumeSnapshot s;
  s.kind = ResumeKind::kMission;
  s.location = location;
  s.slot = slot;
  s.mission = capture_mission_save(install, mission, slot.number, slot.name, session);
  return s;
}

ResumeSnapshot patrol_resume_snapshot(Mission& mission, Patrol& patrol, const SessionStateParts& session) {
  Career& live = patrol.career();
  std::vector<std::uint8_t> state = live.patrol_state;
  const std::uint8_t version = live.patrol_state_version;
  const std::int32_t log_count = live.record.patrol_log_count;
  capture_patrol_state(mission, patrol, session);
  ResumeSnapshot s;
  s.kind = ResumeKind::kPatrol;
  s.location = {FrontEndPlace::kCareerBase, live.record.roster_slot};
  s.career = live;
  live.patrol_state = std::move(state);
  live.patrol_state_version = version;
  live.record.patrol_log_count = log_count;
  return s;
}

bool write_resume_snapshot(const SaveDir& saves, const ResumeSnapshot& snapshot) {
  return saves.write(kResumeFileName, snapshot.serialize());
}

std::optional<ResumeSnapshot> take_resume_snapshot(const SaveDir& saves) {
  const std::optional<std::vector<std::uint8_t>> data = saves.read(kResumeFileName);
  // An empty file is a deleted one (`SaveDir::remove`).
  if (!data || data->empty()) {
    return std::nullopt;
  }
  delete_resume_snapshot(saves);
  return ResumeSnapshot::parse(*data);
}

void delete_resume_snapshot(const SaveDir& saves) {
  if (const std::optional<std::vector<std::uint8_t>> data = saves.read(kResumeFileName); data && !data->empty()) {
    saves.remove(kResumeFileName);
  }
}

}  // namespace sh
