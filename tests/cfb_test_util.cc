// Copyright 2026 The doccraft Authors. All Rights Reserved.
#include "cfb_test_util.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace doccraft::cfb::test {
namespace {
constexpr uint32_t kFree = 0xFFFFFFFF;
constexpr uint32_t kEnd = 0xFFFFFFFE;
constexpr uint32_t kFat = 0xFFFFFFFD;
constexpr uint32_t kDifat = 0xFFFFFFFC;
void Put16(std::vector<uint8_t>& out, size_t at, uint16_t value) {
  out[at] = static_cast<uint8_t>(value);
  out[at + 1] = static_cast<uint8_t>(value >> 8);
}
void Put32(std::vector<uint8_t>& out, size_t at, uint32_t value) {
  for (size_t i = 0; i < 4; ++i) out[at + i] = static_cast<uint8_t>(value >> (8 * i));
}
void Put64(std::vector<uint8_t>& out, size_t at, uint64_t value) {
  Put32(out, at, static_cast<uint32_t>(value));
  Put32(out, at + 4, static_cast<uint32_t>(value >> 32));
}
uint32_t Ceil(uint64_t n, uint32_t unit) {
  return static_cast<uint32_t>((n + unit - 1) / unit);
}
struct Node {
  std::string name;
  uint8_t type;
  std::vector<uint8_t> data;
  uint32_t left = kFree;
  uint32_t right = kFree;
  uint32_t child = kFree;
  uint32_t start = kEnd;
  uint8_t color = 1;
};
uint32_t Tree(std::vector<Node>& nodes, std::vector<uint32_t> ids) {
  if (ids.empty()) return kFree;
  std::sort(ids.begin(), ids.end(), [&](uint32_t a, uint32_t b) {
    return nodes[a].name < nodes[b].name;
  });
  const size_t mid = ids.size() / 2;
  uint32_t root = ids[mid];
  nodes[root].left = Tree(nodes, {ids.begin(), ids.begin() + static_cast<ptrdiff_t>(mid)});
  nodes[root].right = Tree(nodes, {ids.begin() + static_cast<ptrdiff_t>(mid + 1), ids.end()});
  return root;
}
uint32_t Depth(const std::vector<Node>& nodes, uint32_t id) {
  if (id == kFree) return 0;
  return 1 + std::max(Depth(nodes, nodes[id].left), Depth(nodes, nodes[id].right));
}
void ColorLeaves(std::vector<Node>& nodes, uint32_t id, uint32_t depth) {
  if (id == kFree) return;
  if (depth == 1) nodes[id].color = 0;
  ColorLeaves(nodes, nodes[id].left, depth - 1);
  ColorLeaves(nodes, nodes[id].right, depth - 1);
}
}  // namespace

std::vector<uint8_t> MakePayload(size_t size, uint8_t fill) {
  return std::vector<uint8_t>(size, fill);
}
CfbBuilder& CfbBuilder::AddStream(const std::string& name, std::vector<uint8_t> data) {
  streams_.push_back({name, std::move(data)});
  return *this;
}
CfbBuilder& CfbBuilder::SetSectorSize(uint32_t size) {
  if (size != 512 && size != 4096) throw std::invalid_argument("sector size");
  sector_size_ = size;
  return *this;
}
std::vector<uint8_t> CfbBuilder::Build() {
  std::vector<Node> nodes{{"Root Entry", 5, {}}};
  std::vector<std::vector<uint32_t>> children(1);
  for (const auto& spec : streams_) {
    uint32_t parent = 0;
    size_t begin = 0;
    while (true) {
      size_t slash = spec.name.find('/', begin);
      std::string part = spec.name.substr(begin, slash - begin);
      if (part.empty() || part.size() > 31) throw std::invalid_argument("entry name");
      uint32_t found = kFree;
      for (uint32_t id : children[parent]) {
        if (nodes[id].name == part) found = id;
      }
      if (found == kFree) {
        found = static_cast<uint32_t>(nodes.size());
        nodes.push_back({part, static_cast<uint8_t>(slash == std::string::npos ? 2 : 1), {}});
        children.emplace_back();
        children[parent].push_back(found);
      }
      if (slash == std::string::npos) {
        nodes[found].data = spec.data;
        break;
      }
      parent = found;
      begin = slash + 1;
    }
  }
  for (size_t i = 0; i < nodes.size(); ++i) {
    nodes[i].child = Tree(nodes, children[i]);
    uint32_t depth = Depth(nodes, nodes[i].child);
    if (depth > 1) ColorLeaves(nodes, nodes[i].child, depth);
  }
  std::vector<uint32_t> mini_fat;
  for (auto& node : nodes) {
    if (node.type != 2 || node.data.empty() || node.data.size() >= 4096) continue;
    node.start = static_cast<uint32_t>(mini_fat.size());
    uint32_t count = Ceil(node.data.size(), 64);
    for (uint32_t j = 0; j < count; ++j) {
      mini_fat.push_back(j + 1 == count ? kEnd : node.start + j + 1);
    }
    size_t old = nodes[0].data.size();
    nodes[0].data.resize(old + static_cast<size_t>(count) * 64);
    std::copy(node.data.begin(), node.data.end(), nodes[0].data.begin() + static_cast<ptrdiff_t>(old));
  }
  uint32_t next = 0;
  const uint32_t dir_start = next;
  const uint32_t dir_count = Ceil(nodes.size() * 128, sector_size_);
  next += dir_count;
  const uint32_t mini_start = mini_fat.empty() ? kEnd : next;
  const uint32_t mini_count = Ceil(mini_fat.size() * 4, sector_size_);
  next += mini_count;
  for (auto& node : nodes) {
    if (node.data.empty() || (node.type == 2 && node.data.size() < 4096)) continue;
    node.start = next;
    next += Ceil(node.data.size(), sector_size_);
  }
  uint32_t fat_count = 0;
  uint32_t difat_count = 0;
  const uint32_t per_fat = sector_size_ / 4;
  do {
    uint32_t f = Ceil(static_cast<uint64_t>(next) + fat_count + difat_count, per_fat);
    uint32_t d = f <= 109 ? 0 : Ceil(f - 109, per_fat - 1);
    if (f == fat_count && d == difat_count) break;
    fat_count = f;
    difat_count = d;
  } while (true);
  const uint32_t fat_start = next;
  const uint32_t difat_start = difat_count ? next + fat_count : kEnd;
  uint32_t total = next + fat_count + difat_count;
  std::vector<uint8_t> out((static_cast<size_t>(total) + 1) * sector_size_, 0);
  std::vector<uint32_t> fat(total, kFree);
  auto chain = [&](uint32_t start, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) fat[start + i] = i + 1 == count ? kEnd : start + i + 1;
  };
  chain(dir_start, dir_count);
  if (mini_count) chain(mini_start, mini_count);
  for (const auto& node : nodes) {
    if (node.start == kEnd || (node.type == 2 && node.data.size() < 4096)) continue;
    uint32_t count = Ceil(node.data.size(), sector_size_);
    chain(node.start, count);
    std::copy(node.data.begin(), node.data.end(),
              out.begin() + static_cast<ptrdiff_t>((static_cast<size_t>(node.start) + 1) * sector_size_));
  }
  for (uint32_t i = 0; i < fat_count; ++i) fat[fat_start + i] = kFat;
  for (uint32_t i = 0; i < difat_count; ++i) fat[difat_start + i] = kDifat;
  for (size_t i = 0; i < nodes.size(); ++i) {
    const Node& n = nodes[i];
    size_t at = (static_cast<size_t>(dir_start) + 1) * sector_size_ + i * 128;
    for (size_t j = 0; j < n.name.size(); ++j) Put16(out, at + j * 2, static_cast<uint8_t>(n.name[j]));
    Put16(out, at + n.name.size() * 2, 0);
    Put16(out, at + 64, static_cast<uint16_t>((n.name.size() + 1) * 2));
    out[at + 66] = n.type;
    out[at + 67] = n.color;
    Put32(out, at + 68, n.left);
    Put32(out, at + 72, n.right);
    Put32(out, at + 76, n.child);
    Put32(out, at + 116, n.start);
    Put64(out, at + 120, n.data.size());
  }
  for (size_t i = 0; i < mini_fat.size(); ++i) {
    Put32(out, (static_cast<size_t>(mini_start) + 1) * sector_size_ + i * 4, mini_fat[i]);
  }
  for (uint32_t i = 0; i < fat_count * per_fat; ++i) {
    Put32(out, (static_cast<size_t>(fat_start + i / per_fat) + 1) * sector_size_ + (i % per_fat) * 4,
          i < fat.size() ? fat[i] : kFree);
  }
  for (uint32_t i = 0; i < difat_count; ++i) {
    size_t at = (static_cast<size_t>(difat_start + i) + 1) * sector_size_;
    for (uint32_t j = 0; j < per_fat - 1; ++j) {
      uint32_t index = 109 + i * (per_fat - 1) + j;
      Put32(out, at + j * 4, index < fat_count ? fat_start + index : kFree);
    }
    Put32(out, at + sector_size_ - 4, i + 1 == difat_count ? kEnd : difat_start + i + 1);
  }
  const uint8_t sig[] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
  std::copy(std::begin(sig), std::end(sig), out.begin());
  Put16(out, 24, 0x003E);
  Put16(out, 26, sector_size_ == 512 ? 3 : 4);
  Put16(out, 28, 0xFFFE);
  Put16(out, 30, sector_size_ == 512 ? 9 : 12);
  Put16(out, 32, 6);
  Put32(out, 40, sector_size_ == 512 ? 0 : dir_count);
  Put32(out, 44, fat_count);
  Put32(out, 48, dir_start);
  Put32(out, 56, 4096);
  Put32(out, 60, mini_start);
  Put32(out, 64, mini_count);
  Put32(out, 68, difat_start);
  Put32(out, 72, difat_count);
  for (uint32_t i = 0; i < 109; ++i) Put32(out, 76 + i * 4, i < fat_count ? fat_start + i : kFree);
  return out;
}
}  // namespace doccraft::cfb::test
