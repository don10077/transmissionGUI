// GeoIP Country Edition database reader.
//
// Faithful port of the reference GeoIP reader (see WORKFLOW.md): a fixed-size
// pointer tree maps an IPv4 address to an offset into a country segment; that
// offset minus COUNTRY_BEGIN is the index into the standard 253-entry
// country-code list. Only the Country Edition is supported (the reference's
// default database type), which is what the peers-tab country display needs.
#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace tgui {

class GeoIpDb {
public:
  // kCountryBegin is a threshold value used to distinguish tree node pointers
  // (value < kCountryBegin) from country code indices (value >= kCountryBegin).
  // It is NOT a minimum file size requirement.
  static constexpr std::size_t kCountryBegin = 16776960;

  // Load the raw database from a file. Returns false if it cannot be read or
  // is too small to contain at least one tree node.
  bool load(const std::filesystem::path& file);
  // Load an in-memory copy (used by tests and by the database downloader).
  bool load(std::vector<unsigned char> data);

  bool isLoaded() const { return m_loaded; }
  std::size_t size() const { return m_data.size(); }

  // Resolve an IPv4 address to a 2-letter ISO country code ("" if the database
  // is absent, the address is invalid, or the lookup has no data).
  std::string countryFor(const std::string& ipv4) const;

  // Map a 2-letter country code to the full English country name
  // ("" if unknown). Mirrors the reference's CountryNames array.
  static std::string countryNameFor(const std::string& code);

  // True if the string is a syntactically valid dotted-quad IPv4 address.
  static bool isValidIpv4(const std::string& ipv4);

private:
  std::vector<unsigned char> m_data;
  bool m_loaded = false;
};

} // namespace tgui