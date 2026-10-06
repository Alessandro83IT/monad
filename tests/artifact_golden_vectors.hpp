// SPDX-FileCopyrightText: 2026 Alessandro Bifaretti
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "artifact/model.hpp"
#include <cstddef>
// Canonical hex transcribed independently from Artifact Schema v1. Digest
// constants calculated with Python hashlib and cross-checked with openssl dgst.
// Header constants: tree marker (including NUL), U32 version, U64 count.
// Each entry: tag, U64 path length, path, then boolean/content or target frame.
struct GoldenVector {
    const char* name;
    athena::artifact::NormalizedArtifact model;
    const char* canonical_hex;
    std::size_t length;
    const char* canonical_sha256;
    const char* artifact_id;
};
inline const GoldenVector golden_vectors[] = {
    {"empty", {},
     "617468656e612d61727469666163742d7472656500"
     "00000001"
     "0000000000000000", 33,
     "ca0c4e61cb7e9ce6ebbe94aa01b67cd5a56bb7a1155164a11208257b016eaf55",
     "sha256:48cb081520a2f9e719c68d0fd77c4b5e2c04ae44dd04c689d4121df3611b5f54"},
    {"directory", {1, {{"empty", athena::artifact::Directory{}}}},
     "617468656e612d61727469666163742d7472656500"
     "00000001"
     "0000000000000001"
     "010000000000000005656d707479", 47,
     "b128d85b8c17ec64d36de4e1f0af2b1fd4d007da9c2f67d52447053cce0c27a8",
     "sha256:315ae63299a7339763546ed5c7f17584a6c395c79d06bb219089912c0a2e22a5"},
    {"ascii", {1, {{"a", athena::artifact::RegularFile{false, "hi"}}}},
     "617468656e612d61727469666163742d7472656500"
     "00000001"
     "0000000000000001"
     "02000000000000000161"
     "00"
     "00000000000000026869", 54,
     "a0532ac65d9d960cbd4e2adb1739495ce6785f6bb2c823ada721e19c4ee176b8",
     "sha256:9a44b9ff03c2c417078c278d32d26de98affd3710c07b60257d0c151f34cc627"},
    {"executable", {1, {{"a", athena::artifact::RegularFile{true, "hi"}}}},
     "617468656e612d61727469666163742d7472656500"
     "00000001"
     "0000000000000001"
     "02000000000000000161"
     "01"
     "00000000000000026869", 54,
     "3db776f5e7be766127b9523abe6a783be2c265c5b1a51fb78dd204ec499936d3",
     "sha256:e617b5a918778e3593b8fd15bb2959243d906c12e4bb05b11fc2459ed7ab7174"},
    {"binary", {1, {{"a", athena::artifact::RegularFile{false, std::string("\0\x80\xff", 3)}}}},
     "617468656e612d61727469666163742d7472656500"
     "00000001"
     "0000000000000001"
     "02000000000000000161"
     "00"
     "00000000000000030080ff", 55,
     "29f366441b75e4b58f8dbfef9eb0579195078232621215b8f52cc21f2b524471",
     "sha256:40fca22f9ea15546e2f9a6385b0bc5a7b2d99c702b4419464d53eb3f1345dde3"},
    {"relative", {1, {{"l", athena::artifact::SymbolicLink{"a"}}}},
     "617468656e612d61727469666163742d7472656500"
     "00000001"
     "0000000000000001"
     "0300000000000000016c"
     "000000000000000161", 52,
     "f34f683602f009d50867951ee7c0eb267e31029b70e0a462d196ddc374ef3470",
     "sha256:81f483ae2edbb77af465cdc4ef1f36207b0768114445f9bbb0e25d2490373ecb"},
    {"parent", {1, {{"l", athena::artifact::SymbolicLink{"../a"}}}},
     "617468656e612d61727469666163742d7472656500"
     "00000001"
     "0000000000000001"
     "0300000000000000016c"
     "00000000000000042e2e2f61", 55,
     "5abb42c501f683419afecd65c889e04b49091742a30283752e9be49c4144a71e",
     "sha256:f511e2c3278e6f3b495f0ba88b135e7a52881d54427961b957d91aca55a00ae3"},
    {"absolute", {1, {{"l", athena::artifact::SymbolicLink{"/a"}}}},
     "617468656e612d61727469666163742d7472656500"
     "00000001"
     "0000000000000001"
     "0300000000000000016c"
     "00000000000000022f61", 53,
     "48a26d255c06a324ec3ea50ee36b28e7e894d29980e9324ba38437cdce6b71e1",
     "sha256:264fbcb4fb00cc1cd64c856a9096023a06053b957b2995c81fbbbda9dae1693a"},
    {"mixed", {1, {{"bin", athena::artifact::Directory{}}, {"bin/tool", athena::artifact::RegularFile{true, "hi\n"}}, {"empty", athena::artifact::Directory{}}, {"l", athena::artifact::SymbolicLink{"../bin/tool"}}}},
     "617468656e612d61727469666163742d7472656500"
     "00000001"
     "0000000000000004"
     "01000000000000000362696e"
     "02000000000000000862696e2f746f6f6c"
     "01"
     "000000000000000368690a"
     "010000000000000005656d707479"
     "0300000000000000016c"
     "000000000000000b2e2e2f62696e2f746f6f6c", 117,
     "573ce601cf7f489ba30abdccd78761acb617d433196efb5f1e04abf440b0f050",
     "sha256:5f1f722957147a8b8effd875bfe137eea89fb94e77a0cac8644e5e7e60fda70b"},
    {"byte_path", {1, {{std::string(1, '\xff'), athena::artifact::SymbolicLink{std::string("/\x80", 2)}}}},
     "617468656e612d61727469666163742d7472656500"
     "00000001"
     "0000000000000001"
     "030000000000000001ff"
     "00000000000000022f80", 53,
     "5aad67b0e2cad88c0d1431d2b8e57c51b2bb0457c53590c3719c7980cbba61c5",
     "sha256:fc799408014b469ed260db22bef4bdea76882997b78d3de82410aa33c13baaf2"},
};
