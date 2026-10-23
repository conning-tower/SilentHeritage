#include "core/byte_source.h"

#include <algorithm>
#include <format>
#include <fstream>
#include <limits>
#include <utility>

#include "core/error.h"

namespace sh {

namespace {

// Throws `DataError` unless `n` bytes at `offset` lie within `size`.
void require_within(const ByteSource& source, std::uint64_t offset, std::uint64_t n) {
  if (offset > source.size() || n > source.size() - offset) {
    throw DataError(std::format("{}: {} bytes at {} are past its end ({} bytes)", source.describe(), n, offset,
                                source.size()));
  }
}

class HostFileSource final : public ByteSource {
 public:
  explicit HostFileSource(const std::filesystem::path& path)
      : path_(path), in_(path, std::ios::binary) {
    std::error_code ec;
    if (!in_ || std::filesystem::is_directory(path, ec)) {
      throw DataError(std::format("cannot open {}", path.string()));
    }
    in_.seekg(0, std::ios::end);
    size_ = static_cast<std::uint64_t>(in_.tellg());
  }

  std::uint64_t size() const override { return size_; }

  void read(std::uint64_t offset, std::span<std::uint8_t> out) const override {
    require_within(*this, offset, out.size());
    in_.clear();
    in_.seekg(static_cast<std::streamoff>(offset));
    if (!in_.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()))) {
      throw DataError(std::format("cannot read {}", path_.string()));
    }
  }

  std::string describe() const override { return path_.string(); }

 private:
  std::filesystem::path path_;
  mutable std::ifstream in_;
  std::uint64_t size_ = 0;
};

class RangeSource final : public ByteSource {
 public:
  RangeSource(std::shared_ptr<const ByteSource> source, std::uint64_t offset, std::uint64_t size, std::string name)
      : source_(std::move(source)), offset_(offset), size_(size), name_(std::move(name)) {}

  std::uint64_t size() const override { return size_; }

  void read(std::uint64_t offset, std::span<std::uint8_t> out) const override {
    require_within(*this, offset, out.size());
    source_->read(offset_ + offset, out);
  }

  std::string describe() const override { return name_; }

 private:
  std::shared_ptr<const ByteSource> source_;
  std::uint64_t offset_;
  std::uint64_t size_;
  std::string name_;
};

class MemorySource final : public ByteSource {
 public:
  MemorySource(std::vector<std::uint8_t> data, std::string name) : data_(std::move(data)), name_(std::move(name)) {}

  std::uint64_t size() const override { return data_.size(); }

  void read(std::uint64_t offset, std::span<std::uint8_t> out) const override {
    require_within(*this, offset, out.size());
    std::copy_n(data_.begin() + static_cast<std::ptrdiff_t>(offset), out.size(), out.begin());
  }

  std::string describe() const override { return name_; }

 private:
  std::vector<std::uint8_t> data_;
  std::string name_;
};

}  // namespace

std::vector<std::uint8_t> ByteSource::read_bytes(std::uint64_t offset, std::size_t size) const {
  std::vector<std::uint8_t> out(size);
  read(offset, std::span(out));
  return out;
}

std::vector<std::uint8_t> ByteSource::read_all() const {
  if (size() > std::numeric_limits<std::size_t>::max()) {
    throw DataError(std::format("{} is too large to read", describe()));
  }
  return read_bytes(0, static_cast<std::size_t>(size()));
}

std::shared_ptr<const ByteSource> host_file_source(const std::filesystem::path& path) {
  return std::make_shared<HostFileSource>(path);
}

std::shared_ptr<const ByteSource> byte_range(std::shared_ptr<const ByteSource> source, std::uint64_t offset,
                                             std::uint64_t size, std::string name) {
  require_within(*source, offset, size);
  return std::make_shared<RangeSource>(std::move(source), offset, size, std::move(name));
}

std::shared_ptr<const ByteSource> memory_source(std::vector<std::uint8_t> data, std::string name) {
  return std::make_shared<MemorySource>(std::move(data), std::move(name));
}

}  // namespace sh
