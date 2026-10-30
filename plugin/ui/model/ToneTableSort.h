#pragma once

#include "Tone.h"
#include <cstddef>
#include <optional>
#include <vector>

namespace t3k::ui {

enum class ToneColumn { title, creator, gear, format, models, downloads, favorites, published };
struct ToneColumnSort {
  ToneColumn column = ToneColumn::title;
  bool ascending = true;
};

const char* toneColumnName(ToneColumn);

/** A row-index projection; never mutates the catalog or Tone.raw. No sort
    restores the original API order. Missing creator/date values remain last
    in either direction. Equal keys use Tone.id then original row index. */
std::vector<size_t> toneTableOrder(const std::vector<Tone>&,
                                  std::optional<ToneColumnSort> sort);

} // namespace t3k::ui
