/*
 * Copyright (C) 2024 VioletXF, khoeun03
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Chart.h"
#include <utility>
namespace bms_parser {
Chart::Chart() = default;

Chart::Chart(Chart &&other) noexcept : Chart() { Swap(other); }

Chart &Chart::operator=(Chart &&other) noexcept {
  if (this != &other) {
    Chart incoming(std::move(other));
    Swap(incoming);
  }
  return *this;
}

void Chart::Swap(Chart &other) noexcept {
  using std::swap;
  swap(Meta, other.Meta);
  Measures.swap(other.Measures);
  DetachedNotes.swap(other.DetachedNotes);
  WavTable.swap(other.WavTable);
  ReferencedWavTable.swap(other.ReferencedWavTable);
  BmpTable.swap(other.BmpTable);
  ReferencedBmpTable.swap(other.ReferencedBmpTable);
}

Chart::~Chart() {
  for (const auto &measure : Measures) {
    delete measure;
  }

  Measures.clear();
}
} // namespace bms_parser
