#include "synthetic/media_writer.h"

#include <algorithm>
#include <array>
#include <bit>
#include <map>
#include <optional>
#include <queue>
#include <stdexcept>
#include <utility>

#include "core/crc32.h"

namespace sh::test {

namespace {

// Writes bits most significant first.
class BitWriter {
 public:
  void put(std::uint32_t value, int n) {
    for (int i = n - 1; i >= 0; --i) {
      acc_ = static_cast<std::uint8_t>((acc_ << 1) | ((value >> i) & 1u));
      if (++count_ == 8) {
        out_.push_back(acc_);
        acc_ = 0;
        count_ = 0;
      }
    }
  }

  std::vector<std::uint8_t> finish() {
    if (count_ != 0) {
      out_.push_back(static_cast<std::uint8_t>(acc_ << (8 - count_)));
      count_ = 0;
    }
    return std::move(out_);
  }

 private:
  std::vector<std::uint8_t> out_;
  std::uint8_t acc_ = 0;
  int count_ = 0;
};

// A literal (`length` 0) or a match.
struct Token {
  std::uint8_t literal = 0;
  std::size_t length = 0;
  std::size_t distance = 0;
};

// Returns the greedy LZ77 parse of `data` (matches of 3 to `max_length`
// bytes within `window`, among the last 64 positions of the same 3 bytes).
std::vector<Token> parse(std::span<const std::uint8_t> data, std::size_t max_length, std::size_t window) {
  constexpr int kCandidates = 64;
  std::map<std::uint32_t, std::vector<std::size_t>> positions;  // by 3-byte prefix
  const auto key = [&data](std::size_t i) {
    return static_cast<std::uint32_t>(data[i] | (data[i + 1] << 8) | (data[i + 2] << 16));
  };
  std::vector<Token> out;
  std::size_t i = 0;
  const auto remember = [&](std::size_t from, std::size_t to) {
    for (std::size_t k = from; k < to && k + 2 < data.size(); ++k) {
      positions[key(k)].push_back(k);
    }
  };
  while (i < data.size()) {
    std::size_t best = 0;
    std::size_t best_distance = 0;
    if (i + 2 < data.size()) {
      const auto it = positions.find(key(i));
      if (it != positions.end()) {
        const std::vector<std::size_t>& list = it->second;
        for (std::size_t c = list.size(); c > 0 && list.size() - c < kCandidates; --c) {
          const std::size_t from = list[c - 1];
          if (i - from > window) {
            break;
          }
          std::size_t n = 0;
          while (n < max_length && i + n < data.size() && data[from + n] == data[i + n]) {
            ++n;
          }
          if (n > best) {
            best = n;
            best_distance = i - from;
          }
        }
      }
    }
    if (best >= 3) {
      out.push_back(Token{.literal = 0, .length = best, .distance = best_distance});
      remember(i, i + best);
      i += best;
    } else {
      out.push_back(Token{.literal = data[i], .length = 0, .distance = 0});
      remember(i, i + 1);
      ++i;
    }
  }
  return out;
}

// Returns the Huffman code lengths of `freq` (0 for unused symbols; a
// single used symbol gets length 0 too, written in the one-symbol form).
std::vector<std::uint8_t> code_lengths(const std::vector<std::uint32_t>& freq) {
  std::vector<std::uint8_t> lengths(freq.size(), 0);
  using Node = std::pair<std::uint64_t, int>;
  std::priority_queue<Node, std::vector<Node>, std::greater<>> queue;
  std::vector<int> parent;
  for (std::size_t s = 0; s < freq.size(); ++s) {
    if (freq[s] != 0) {
      queue.emplace(freq[s], static_cast<int>(parent.size()));
      parent.push_back(-1);
    }
  }
  const std::size_t leaves = parent.size();
  if (leaves < 2) {
    return lengths;
  }
  while (queue.size() > 1) {
    const Node a = queue.top();
    queue.pop();
    const Node b = queue.top();
    queue.pop();
    const int node = static_cast<int>(parent.size());
    parent.push_back(-1);
    parent[a.second] = node;
    parent[b.second] = node;
    queue.emplace(a.first + b.first, node);
  }
  std::size_t leaf = 0;
  for (std::size_t s = 0; s < freq.size(); ++s) {
    if (freq[s] == 0) {
      continue;
    }
    int depth = 0;
    for (int n = static_cast<int>(leaf); parent[n] != -1; n = parent[n]) {
      ++depth;
    }
    if (depth > 16) {
      throw std::logic_error("arj_compress: code longer than 16 bits");
    }
    lengths[s] = static_cast<std::uint8_t>(depth);
    ++leaf;
  }
  return lengths;
}

// The canonical codes of `lengths` (shorter codes first, then by symbol).
std::vector<std::uint32_t> canonical_codes(const std::vector<std::uint8_t>& lengths) {
  std::array<std::uint32_t, 18> count{};
  for (const std::uint8_t l : lengths) {
    ++count[l];
  }
  count[0] = 0;
  std::array<std::uint32_t, 18> next{};
  for (int l = 1; l <= 16; ++l) {
    next[l + 1] = (next[l] + count[l]) << 1;
  }
  std::vector<std::uint32_t> codes(lengths.size(), 0);
  for (std::size_t s = 0; s < lengths.size(); ++s) {
    if (lengths[s] != 0) {
      codes[s] = next[lengths[s]]++;
    }
  }
  return codes;
}

// A code: the lengths and codes of its symbols, the one symbol when only
// one is used.
struct Code {
  std::vector<std::uint8_t> lengths;
  std::vector<std::uint32_t> codes;
  int single = -1;

  explicit Code(const std::vector<std::uint32_t>& freq) : lengths(code_lengths(freq)), codes(canonical_codes(lengths)) {
    const auto used = std::ranges::count_if(freq, [](std::uint32_t f) { return f != 0; });
    if (used <= 1) {
      const auto it = std::ranges::find_if(freq, [](std::uint32_t f) { return f != 0; });
      single = it == freq.end() ? 0 : static_cast<int>(it - freq.begin());
    }
  }

  void put(BitWriter& out, std::size_t symbol) const {
    if (single < 0) {
      out.put(codes[symbol], lengths[symbol]);
    }
  }
};

// Writes the lengths of `code` as `read_pt_len` reads them.
void put_pt_len(BitWriter& out, const Code& code, int nbit, int special) {
  if (code.single >= 0) {
    out.put(0, nbit);
    out.put(static_cast<std::uint32_t>(code.single), nbit);
    return;
  }
  int n = static_cast<int>(code.lengths.size());
  while (n > 0 && code.lengths[n - 1] == 0) {
    --n;
  }
  out.put(static_cast<std::uint32_t>(n), nbit);
  for (int i = 0; i < n;) {
    const int c = code.lengths[i++];
    if (c < 7) {
      out.put(static_cast<std::uint32_t>(c), 3);
    } else {
      out.put(7, 3);
      out.put((1u << (c - 7)) - 1, c - 7);
      out.put(0, 1);
    }
    if (i == special) {
      int zeros = 0;
      while (zeros < 3 && i + zeros < n && code.lengths[i + zeros] == 0) {
        ++zeros;
      }
      out.put(static_cast<std::uint32_t>(zeros), 2);
      i += zeros;
    }
  }
}

constexpr int kNc = 510;
constexpr int kNp = 17;
constexpr int kNt = 19;

// The symbol and extra bits of the length codes of `c_len`, as
// `read_c_len` reads them.
struct LenSymbol {
  int symbol;
  std::uint32_t extra;
  int extra_bits;
};

std::vector<LenSymbol> c_len_symbols(const std::vector<std::uint8_t>& c_len, int n) {
  std::vector<LenSymbol> out;
  for (int i = 0; i < n;) {
    if (c_len[i] != 0) {
      out.push_back({c_len[i] + 2, 0, 0});
      ++i;
      continue;
    }
    int run = 0;
    while (i + run < n && c_len[i + run] == 0) {
      ++run;
    }
    i += run;
    while (run > 0) {
      if (run >= 20) {
        const int k = std::min(run - 20, 511);
        out.push_back({2, static_cast<std::uint32_t>(k), 9});
        run -= k + 20;
      } else if (run >= 3) {
        const int k = std::min(run, 18);
        out.push_back({1, static_cast<std::uint32_t>(k - 3), 4});
        run -= k;
      } else {
        out.push_back({0, 0, 0});
        --run;
      }
    }
  }
  return out;
}

// The distance symbol of `distance` and its extra bits.
std::pair<int, int> p_symbol(std::size_t distance) {
  const auto v = static_cast<std::uint32_t>(distance - 1);
  return v == 0 ? std::pair(0, 0) : std::pair(static_cast<int>(std::bit_width(v)), static_cast<int>(std::bit_width(v)) - 1);
}

std::vector<std::uint8_t> compress_huffman(std::span<const std::uint8_t> data) {
  const std::vector<Token> tokens = parse(data, 256, 26624);
  BitWriter out;
  for (std::size_t start = 0; start < tokens.size(); start += 65535) {
    const std::span<const Token> block =
        std::span(tokens).subspan(start, std::min<std::size_t>(65535, tokens.size() - start));
    std::vector<std::uint32_t> c_freq(kNc, 0);
    std::vector<std::uint32_t> p_freq(kNp, 0);
    for (const Token& t : block) {
      if (t.length == 0) {
        ++c_freq[t.literal];
      } else {
        ++c_freq[t.length + 253];
        ++p_freq[p_symbol(t.distance).first];
      }
    }
    const Code c_code(c_freq);
    const Code p_code(p_freq);
    int n = kNc;
    while (n > 0 && c_code.lengths[n - 1] == 0) {
      --n;
    }
    const std::vector<LenSymbol> len_symbols = c_len_symbols(c_code.lengths, n);
    std::vector<std::uint32_t> t_freq(kNt, 0);
    for (const LenSymbol& s : len_symbols) {
      ++t_freq[s.symbol];
    }
    const Code t_code(t_freq);

    out.put(static_cast<std::uint32_t>(block.size()), 16);
    if (c_code.single >= 0) {
      // One symbol: the length codes are not read.
      put_pt_len(out, t_code, 5, 3);
      out.put(0, 9);
      out.put(static_cast<std::uint32_t>(c_code.single), 9);
    } else {
      put_pt_len(out, t_code, 5, 3);
      out.put(static_cast<std::uint32_t>(n), 9);
      for (const LenSymbol& s : len_symbols) {
        t_code.put(out, static_cast<std::size_t>(s.symbol));
        out.put(s.extra, s.extra_bits);
      }
    }
    put_pt_len(out, p_code, 5, -1);
    for (const Token& t : block) {
      if (t.length == 0) {
        c_code.put(out, t.literal);
        continue;
      }
      c_code.put(out, t.length + 253);
      const auto [symbol, bits] = p_symbol(t.distance);
      p_code.put(out, static_cast<std::size_t>(symbol));
      if (bits > 0) {
        out.put(static_cast<std::uint32_t>(t.distance - 1) - (1u << bits), bits);
      }
    }
  }
  return out.finish();
}

// Writes `value` in method 4's code of widths `first` to `last`.
void put_fastest_code(BitWriter& out, std::uint32_t value, int first, int last) {
  std::uint32_t plus = 0;
  int width = first;
  while (width < last && value >= plus + (1u << width)) {
    plus += 1u << width;
    out.put(1, 1);
    ++width;
  }
  if (width < last) {
    out.put(0, 1);
  }
  out.put(value - plus, width);
}

std::vector<std::uint8_t> compress_fastest(std::span<const std::uint8_t> data) {
  BitWriter out;
  for (const Token& t : parse(data, 256, 15872)) {
    if (t.length == 0) {
      out.put(0, 1);
      out.put(t.literal, 8);
    } else {
      put_fastest_code(out, static_cast<std::uint32_t>(t.length - 2), 0, 7);
      put_fastest_code(out, static_cast<std::uint32_t>(t.distance - 1), 9, 13);
    }
  }
  return out.finish();
}

void put16(std::vector<std::uint8_t>& out, std::uint32_t v) {
  out.push_back(static_cast<std::uint8_t>(v));
  out.push_back(static_cast<std::uint8_t>(v >> 8));
}

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  put16(out, v & 0xffff);
  put16(out, v >> 16);
}

// Appends a header block (id, size, basic header, CRC, no extended header).
void put_header(std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& basic) {
  out.push_back(0x60);
  out.push_back(0xea);
  put16(out, static_cast<std::uint32_t>(basic.size()));
  out.insert(out.end(), basic.begin(), basic.end());
  put32(out, crc32(basic));
  put16(out, 0);
}

// Returns a basic header: the fixed part (30 or 34 bytes), name, comment.
std::vector<std::uint8_t> basic_header(std::uint8_t flags, std::uint8_t method, std::uint8_t type,
                                       std::uint32_t date, std::uint32_t packed, std::uint32_t original,
                                       std::uint32_t crc, std::optional<std::uint32_t> continued_at,
                                       const std::string& name) {
  std::vector<std::uint8_t> h;
  h.push_back(continued_at ? 34 : 30);
  h.push_back(6);  // archiver version
  h.push_back(1);  // minimum version
  h.push_back(0);  // MS-DOS
  h.push_back(flags);
  h.push_back(method);
  h.push_back(type);
  h.push_back(0);
  put32(h, date);
  put32(h, packed);
  put32(h, original);
  put32(h, crc);
  put16(h, 0);  // filespec position
  put16(h, 0x20);  // access mode
  put16(h, 0);  // host data
  if (continued_at) {
    put32(h, *continued_at);
  }
  for (const char c : name) {
    h.push_back(static_cast<std::uint8_t>(c == '/' ? '\\' : c));
  }
  h.push_back(0);
  h.push_back(0);  // the comment
  return h;
}

}  // namespace

std::vector<std::uint8_t> arj_compress(std::span<const std::uint8_t> data, std::uint8_t method) {
  switch (method) {
    case 0:
      return {data.begin(), data.end()};
    case 1:
    case 2:
    case 3:
      return compress_huffman(data);
    case 4:
      return compress_fastest(data);
    default:
      throw std::logic_error("arj_compress: unknown method");
  }
}

std::vector<std::uint8_t> write_arj(const std::string& name, const std::vector<ArjFileSpec>& files,
                                    std::span<const std::uint8_t> prefix) {
  std::vector<std::uint8_t> out(prefix.begin(), prefix.end());
  put_header(out, basic_header(0x10, 0, 2, 0x2322546f, 0x2322546f, 0, 0, std::nullopt, name));
  for (const ArjFileSpec& f : files) {
    const std::vector<std::uint8_t> packed = f.directory ? std::vector<std::uint8_t>() : arj_compress(f.data, f.method);
    const auto original = static_cast<std::uint32_t>(f.directory ? 0 : f.data.size());
    const std::optional<std::uint32_t> continued_at =
        (f.flags & 0x08) != 0 ? std::optional(f.continued_at) : std::nullopt;
    put_header(out, basic_header(f.flags, f.directory ? 0 : f.method, f.directory ? 3 : 0, 0x2322546f,
                                 static_cast<std::uint32_t>(packed.size()), original, crc32(f.data), continued_at,
                                 f.path));
    out.insert(out.end(), packed.begin(), packed.end());
  }
  out.push_back(0x60);
  out.push_back(0xea);
  put16(out, 0);
  return out;
}

namespace {

constexpr std::size_t kSector = 2048;

// Returns a directory record.
std::vector<std::uint8_t> iso_record(const std::string& name, std::uint32_t sector, std::uint32_t size,
                                     bool directory) {
  std::vector<std::uint8_t> r;
  const std::size_t length = 33 + name.size() + (name.size() % 2 == 0 ? 1 : 0);
  r.push_back(static_cast<std::uint8_t>(length));
  r.push_back(0);
  const auto both32 = [&r](std::uint32_t v) {
    put32(r, v);
    for (const int shift : {24, 16, 8, 0}) {
      r.push_back(static_cast<std::uint8_t>(v >> shift));
    }
  };
  both32(sector);
  both32(size);
  for (int i = 0; i < 7; ++i) {
    r.push_back(0);  // date
  }
  r.push_back(directory ? 0x02 : 0x00);
  r.push_back(0);
  r.push_back(0);
  put16(r, 1);  // volume sequence number, both byte orders
  r.push_back(0);
  r.push_back(1);
  r.push_back(static_cast<std::uint8_t>(name.size()));
  r.insert(r.end(), name.begin(), name.end());
  r.resize(length, 0);
  return r;
}

}  // namespace

std::vector<std::uint8_t> write_iso(const std::vector<IsoFileSpec>& files) {
  // The directories, by path ("" for the root), with their children.
  std::map<std::string, std::vector<std::string>> subdirs;
  std::map<std::string, std::vector<std::size_t>> dir_files;
  subdirs[""];
  for (std::size_t i = 0; i < files.size(); ++i) {
    std::string dir;
    std::string_view rest = files[i].path;
    for (std::size_t slash = rest.find('/'); slash != std::string_view::npos; slash = rest.find('/')) {
      const std::string child = dir.empty() ? std::string(rest.substr(0, slash))
                                            : dir + "/" + std::string(rest.substr(0, slash));
      auto& list = subdirs[dir];
      if (std::ranges::find(list, child) == list.end()) {
        list.push_back(child);
      }
      subdirs[child];
      dir = child;
      rest.remove_prefix(slash + 1);
    }
    dir_files[dir].push_back(i);
  }
  // One sector per directory, then the files.
  std::map<std::string, std::uint32_t> dir_sector;
  std::uint32_t next = 18;
  for (const auto& [dir, list] : subdirs) {
    dir_sector[dir] = next++;
  }
  std::vector<std::uint32_t> file_sector(files.size());
  for (std::size_t i = 0; i < files.size(); ++i) {
    file_sector[i] = next;
    next += static_cast<std::uint32_t>((files[i].data.size() + kSector - 1) / kSector);
  }
  std::vector<std::uint8_t> image(next * kSector, 0);
  const auto base_name = [](const std::string& path) {
    const std::size_t slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
  };
  for (const auto& [dir, list] : subdirs) {
    std::vector<std::uint8_t> records;
    const auto add = [&records](const std::vector<std::uint8_t>& r) { records.insert(records.end(), r.begin(), r.end()); };
    add(iso_record(std::string(1, '\0'), dir_sector[dir], kSector, true));
    add(iso_record(std::string(1, '\1'), dir_sector[dir], kSector, true));
    for (const std::string& child : list) {
      add(iso_record(base_name(child), dir_sector[child], kSector, true));
    }
    for (const std::size_t i : dir_files[dir]) {
      add(iso_record(base_name(files[i].path) + ";1", file_sector[i],
                     static_cast<std::uint32_t>(files[i].data.size()), false));
    }
    if (records.size() > kSector) {
      throw std::logic_error("write_iso: directory larger than a sector");
    }
    std::ranges::copy(records, image.begin() + static_cast<std::ptrdiff_t>(dir_sector[dir] * kSector));
  }
  for (std::size_t i = 0; i < files.size(); ++i) {
    std::ranges::copy(files[i].data, image.begin() + static_cast<std::ptrdiff_t>(file_sector[i] * kSector));
  }
  std::uint8_t* pvd = image.data() + 16 * kSector;
  pvd[0] = 1;
  std::ranges::copy(std::string_view("CD001"), pvd + 1);
  pvd[6] = 1;
  const std::vector<std::uint8_t> root = iso_record(std::string(1, '\0'), dir_sector[""], kSector, true);
  std::ranges::copy(root, pvd + 156);
  std::uint8_t* terminator = image.data() + 17 * kSector;
  terminator[0] = 255;
  std::ranges::copy(std::string_view("CD001"), terminator + 1);
  terminator[6] = 1;
  return image;
}

}  // namespace sh::test
