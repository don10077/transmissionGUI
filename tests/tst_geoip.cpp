#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include <tgui/geoip.h>

using tgui::GeoIpDb;

namespace {

// Build a minimal valid GeoIP Country Edition database (full standard tree
// size) in which IPv4 "8.8.8.8" resolves to country index 2 ("EU") and any
// address whose most-significant bit is set walks to "no data".
std::vector<unsigned char> makeDb() {
  const std::size_t countryBegin = GeoIpDb::kCountryBegin; // 16776960
  std::vector<unsigned char> db(countryBegin + 3, 0);
  const std::uint32_t leaf = static_cast<std::uint32_t>(countryBegin + 2);
  db[0] = static_cast<unsigned char>(leaf & 0xFF);
  db[1] = static_cast<unsigned char>((leaf >> 8) & 0xFF);
  db[2] = static_cast<unsigned char>((leaf >> 16) & 0xFF);
  // x[1] (bytes 3..5) stays 0, so MSB-set addresses resolve to "no data".
  return db;
}

TEST(GeoIpDb, ResolvesKnownIp) {
  GeoIpDb db;
  ASSERT_TRUE(db.load(makeDb()));
  EXPECT_TRUE(db.isLoaded());
  EXPECT_EQ(db.countryFor("8.8.8.8"), "EU"); // bit31=0 -> x[0]=leaf -> idx 2
}

TEST(GeoIpDb, NoDataReturnsEmpty) {
  GeoIpDb db;
  ASSERT_TRUE(db.load(makeDb()));
  // All-ones address takes x[1] (0, a non-leaf) at every level -> no data.
  EXPECT_TRUE(db.countryFor("255.255.255.255").empty());
}

TEST(GeoIpDb, InvalidAddressReturnsEmpty) {
  GeoIpDb db;
  ASSERT_TRUE(db.load(makeDb()));
  EXPECT_TRUE(db.countryFor("not-an-ip").empty());
  EXPECT_TRUE(db.countryFor("999.1.1.1").empty());
  EXPECT_TRUE(db.countryFor("1.2.3.4.5").empty());
  EXPECT_TRUE(db.countryFor("").empty());
}

TEST(GeoIpDb, IsValidIpv4Checks) {
  EXPECT_TRUE(GeoIpDb::isValidIpv4("1.2.3.4"));
  EXPECT_TRUE(GeoIpDb::isValidIpv4("0.0.0.0"));
  EXPECT_FALSE(GeoIpDb::isValidIpv4("1.2.3"));
  EXPECT_FALSE(GeoIpDb::isValidIpv4("256.1.1.1"));
  EXPECT_FALSE(GeoIpDb::isValidIpv4("1.2.3.4.5"));
  EXPECT_FALSE(GeoIpDb::isValidIpv4(""));
}

TEST(GeoIpDb, UnloadedResolvesNothing) {
  GeoIpDb db;
  EXPECT_FALSE(db.isLoaded());
  EXPECT_TRUE(db.countryFor("8.8.8.8").empty());
}

TEST(GeoIpDb, RejectsFileTooSmall) {
  GeoIpDb db;
  EXPECT_FALSE(db.load(std::vector<unsigned char>(5, 0))); // < one tree node (6 bytes)
}

} // namespace