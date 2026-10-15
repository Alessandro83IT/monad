// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "metadata.hpp"
#include <iosfwd>

namespace athena::store::metadata_detail {
// Internal conversion/error boundary; the path-based API opens its own stream.
Metadata read_metadata_stream(std::istream&, const std::filesystem::path& store_directory,
                              std::size_t max_document_bytes);
}
