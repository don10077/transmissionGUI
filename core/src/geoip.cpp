#include <tgui/geoip.h>

#include <fstream>
#include <map>

namespace tgui {
namespace {

constexpr std::size_t kRecordLength = 3;   // bytes per pointer (24 bits)
constexpr std::size_t kRecordSize = 2 * kRecordLength; // 6 bytes per tree record

// Standard GeoIP country-code list (index -> 2-letter code), same order as the
// reference database (see WORKFLOW.md). Index 0 ("--") means "no country".
const char * const kCountryCodes[] = {
    "--", "AP", "EU", "AD", "AE", "AF", "AG", "AI", "AL", "AM", "AN", "AO", "AQ",
    "AR", "AS", "AT", "AU", "AW", "AZ", "BA", "BB", "BD", "BE", "BF", "BG", "BH",
    "BI", "BJ", "BM", "BN", "BO", "BR", "BS", "BT", "BV", "BW", "BY", "BZ", "CA",
    "CC", "CD", "CF", "CG", "CH", "CI", "CK", "CL", "CM", "CN", "CO", "CR", "CU",
    "CV", "CX", "CY", "CZ", "DE", "DJ", "DK", "DM", "DO", "DZ", "EC", "EE", "EG",
    "EH", "ER", "ES", "ET", "FI", "FJ", "FK", "FM", "FO", "FR", "FX", "GA", "GB",
    "GD", "GE", "GF", "GH", "GI", "GL", "GM", "GN", "GP", "GQ", "GR", "GS", "GT",
    "GU", "GW", "GY", "HK", "HM", "HN", "HR", "HT", "HU", "ID", "IE", "IL", "IN",
    "IO", "IQ", "IR", "IS", "IT", "JM", "JO", "JP", "KE", "KG", "KH", "KI", "KM",
    "KN", "KP", "KR", "KW", "KY", "KZ", "LA", "LB", "LC", "LI", "LK", "LR", "LS",
    "LT", "LU", "LV", "LY", "MA", "MC", "MD", "MG", "MH", "MK", "ML", "MM", "MN",
    "MO", "MP", "MQ", "MR", "MS", "MT", "MU", "MV", "MW", "MX", "MY", "MZ", "NA",
    "NC", "NE", "NF", "NG", "NI", "NL", "NO", "NP", "NR", "NU", "NZ", "OM", "PA",
    "PE", "PF", "PG", "PH", "PK", "PL", "PM", "PN", "PR", "PS", "PT", "PW", "PY",
    "QA", "RE", "RO", "RU", "RW", "SA", "SB", "SC", "SD", "SE", "SG", "SH", "SI",
    "SJ", "SK", "SL", "SM", "SN", "SO", "SR", "ST", "SV", "SY", "SZ", "TC", "TD",
    "TF", "TG", "TH", "TJ", "TK", "TM", "TN", "TO", "TL", "TR", "TT", "TV", "TW",
    "TZ", "UA", "UG", "UM", "US", "UY", "UZ", "VA", "VC", "VE", "VG", "VI", "VN",
    "VU", "WF", "WS", "YE", "YT", "RS", "ZA", "ZM", "ME", "ZW", "A1", "A2", "O1",
    "AX", "GG", "IM", "JE", "BL", "MF"};
static_assert(sizeof(kCountryCodes) / sizeof(kCountryCodes[0]) == 253,
              "GeoIP country list must have 253 entries");

bool parseIpv4(const std::string& s, std::uint32_t& out) {
  unsigned oct[4] = {0, 0, 0, 0};
  int idx = 0;
  unsigned value = 0;
  bool seen = false;
  for (char ch : s) {
    if (ch >= '0' && ch <= '9') {
      value = value * 10 + static_cast<unsigned>(ch - '0');
      if (value > 255) return false;
      seen = true;
    } else if (ch == '.') {
      if (!seen || idx > 2) return false;
      oct[idx++] = value;
      value = 0;
      seen = false;
    } else {
      return false;
    }
  }
  if (!seen || idx != 3) return false;
  oct[3] = value;
  out = (std::uint32_t{oct[0]} << 24) | (std::uint32_t{oct[1]} << 16) |
        (std::uint32_t{oct[2]} << 8) | std::uint32_t{oct[3]};
  return true;
}

} // namespace

bool GeoIpDb::load(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary | std::ios::ate);
  if (!in) return false;
  const std::streamoff size = in.tellg();
  if (size <= 0) return false;
  in.seekg(0, std::ios::beg);
  std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
  if (!in.read(reinterpret_cast<char*>(bytes.data()), size)) return false;
  return load(std::move(bytes));
}

bool GeoIpDb::load(std::vector<unsigned char> data) {
  // kCountryBegin is a threshold for distinguishing tree pointers from country
  // code indices, NOT a minimum file size. The tree starts at offset 0 and
  // pointer values >= kCountryBegin encode country codes.
  if (data.size() < kRecordSize) return false; // must contain at least one node
  m_data = std::move(data);
  m_loaded = true;
  return true;
}

std::string GeoIpDb::countryFor(const std::string& ipv4) const {
  if (!m_loaded) return "";
  std::uint32_t ipnum = 0;
  if (!parseIpv4(ipv4, ipnum)) return "";
  std::size_t offset = 0;
  for (int depth = 31; depth >= 0; --depth) {
    const std::size_t pos = offset * kRecordSize;
    if (pos + kRecordSize > m_data.size()) return "";
    std::uint32_t x[2] = {0, 0};
    for (int i = 0; i < 2; ++i)
      for (std::size_t j = 0; j < kRecordLength; ++j)
        x[i] += static_cast<std::uint32_t>(m_data[pos + i * kRecordLength + j])
                << (j * 8);
    const std::uint32_t choice = (ipnum & (1u << depth)) ? x[1] : x[0];
    if (choice >= kCountryBegin) {
      const std::size_t ret = choice - kCountryBegin;
      if (ret == 0 || ret >= sizeof(kCountryCodes) / sizeof(kCountryCodes[0]))
        return "";
      return kCountryCodes[ret];
    }
    offset = choice;
  }
  return "";
}

std::string GeoIpDb::countryNameFor(const std::string& code) {
  static const char * const kNames[] = {
      "", "Asia/Pacific Region", "Europe", "Andorra", "United Arab Emirates",
      "Afghanistan", "Antigua & Barbuda", "Anguilla", "Albania", "Armenia",
      "N. America & Carib.", "Angola", "Antarctica", "Argentina", "American Samoa",
      "Austria", "Australia", "Aruba", "Azerbaijan", "Bosnia & Herzegovina",
      "Barbados", "Bangladesh", "Belgium", "Burkina Faso", "Bulgaria",
      "Bahrain", "Burundi", "Benin", "St. Barthelemy", "Brunei",
      "Bolivia", "Brazil", "Bahamas", "Bhutan", "Bouvet Island",
      "Botswana", "Belarus", "Belize", "Canada", "Cocos & Keeling Is.",
      "Congo (Kinshasa)", "Central African Rep.", "Congo (Brazzaville)",
      "Switzerland", "Cote d'Ivoire", "Cook Is.", "Chile", "Cameroon",
      "China", "Colombia", "Costa Rica", "Cuba", "Cape Verde",
      "Christmas Island", "Cyprus", "Czech Republic", "Germany", "Djibouti",
      "Denmark", "Dominica", "Dominican Rep.", "Algeria", "Ecuador",
      "Estonia", "Egypt", "Western Sahara", "Eritrea", "Spain",
      "Ethiopia", "Finland", "Fiji", "Falkland Is.", "Micronesia",
      "Faroe Is.", "France", "Met. France", "Gabon", "United Kingdom",
      "Grenada", "Georgia", "French Guiana", "Ghana", "Gibraltar",
      "Greenland", "Gambia", "Guinea", "Guadeloupe", "Eq. Guinea",
      "Greece", "S. Geo. & S. Sandw. Is.", "Guatemala", "Guam",
      "Guinea-Bissau", "Guyana", "Hong Kong", "Heard & McDonald Is.",
      "Honduras", "Croatia", "Haiti", "Hungary", "Indonesia",
      "Ireland", "Israel", "India", "Indian Ocean", "Iraq",
      "Iran", "Iceland", "Italy", "Jamaica", "Jordan",
      "Japan", "Kenya", "Kyrgyzstan", "Cambodia", "Kiribati",
      "Comoros", "St. Kitts & Nevis", "North Korea", "South Korea", "Kuwait",
      "Cayman Is.", "Kazakhstan", "Laos", "Lebanon", "St. Lucia",
      "Liechtenstein", "Sri Lanka", "Liberia", "Lesotho", "Lithuania",
      "Luxembourg", "Latvia", "Libya", "Morocco", "Monaco",
      "Moldova", "Madagascar", "Marshall Is.", "Macedonia", "Mali",
      "Myanmar", "Mongolia", "Macau", "N. Mariana Is.", "Martinique",
      "Mauritania", "Montserrat", "Malta", "Mauritius", "Maldives",
      "Malawi", "Mexico", "Malaysia", "Mozambique", "Namibia",
      "New Caledonia", "Niger", "Norfolk Is.", "Nigeria", "Nicaragua",
      "Netherlands", "Norway", "Nepal", "Nauru", "Niue",
      "New Zealand", "Oman", "Panama", "Peru", "Fr. Polynesia",
      "Papua New Guinea", "Philippines", "Pakistan", "Poland", "St. Pierre & Miquelon",
      "Pitcairn Is.", "Puerto Rico", "Palestinian Territory", "Portugal", "Palau",
      "Paraguay", "Qatar", "Reunion", "Romania", "Russia",
      "Rwanda", "Saudi Arabia", "Solomon Is.", "Seychelles", "Sudan",
      "Sweden", "Singapore", "St. Helena", "Slovenia", "Svalbard & Jan Mayen",
      "Slovakia", "Sierra Leone", "San Marino", "Senegal", "Somalia",
      "Suriname", "Sao Tome & Principe", "El Salvador", "Syria", "Swaziland",
      "Turks & Caicos Is.", "Chad", "Fr. Southern & Ant. Lands", "Togo",
      "Thailand", "Tajikistan", "Tokelau", "Turkmenistan", "Tunisia",
      "Tonga", "Timor-Leste", "Turkey", "Trinidad & Tobago", "Tuvalu",
      "Taiwan", "Tanzania", "Ukraine", "Uganda", "US Outlying Is.",
      "United States", "Uruguay", "Uzbekistan", "Vatican City", "St. Vincent & Gren.",
      "Venezuela", "British Virgin Is.", "US Virgin Is.", "Vietnam", "Vanuatu",
      "Wallis & Futuna", "Samoa", "Yemen", "Mayotte", "Serbia",
      "South Africa", "Zambia", "Montenegro", "Zimbabwe", "Anonymous Proxy",
      "Satellite Provider", "Other", "Aland Is.", "Guernsey",
      "Isle of Man", "Jersey", "St. Barthelemy", "St. Martin"};
  static_assert(sizeof(kNames) / sizeof(kNames[0]) == 253, "Must match kCountryCodes");

  // Build index: code -> index in kCountryCodes array
  static std::map<std::string, std::size_t> kIndex = [] {
    std::map<std::string, std::size_t> m;
    for (std::size_t i = 0; i < 253; ++i)
      m[kCountryCodes[i]] = i;
    return m;
  }();

  auto it = kIndex.find(code);
  if (it != kIndex.end())
    return kNames[it->second];
  return "";
}

bool GeoIpDb::isValidIpv4(const std::string& ipv4) {
  std::uint32_t value = 0;
  return parseIpv4(ipv4, value);
}

} // namespace tgui