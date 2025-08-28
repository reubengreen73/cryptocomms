#include "testsys.h"
#include "../IDTypes.h"
#include "../SecretKey.h"
#include "../PeerConfig.h"
#include "../ConfigFileParser.h"

#include <string>
#include <array>
#include <utility>
#include <vector>
#include <set>

const std::string config_path = "../tests/configfileparser-tests/";

/* check that hex strings of the wrong length trigger the correct error */
TESTFUNC(hex_wrong_length)
{
  TESTTHROW(ConfigFileParser(config_path+"config-error-hex-wrong-length-1"),
	    "string is the wrong length");
  TESTTHROW(ConfigFileParser(config_path+"config-error-hex-wrong-length-2"),
	    "string is the wrong length");
}


/* check that an invalid character in a hex string triggers the correct error */
TESTFUNC(hex_invalid_characters)
{
  TESTTHROW(ConfigFileParser(config_path+"config-error-hex-invalid-characters"),
	    "invalid characters present");
}


/* check that an invalid integer string triggers the correct error */
TESTFUNC(int_invalid_number)
{
  TESTTHROW(ConfigFileParser(config_path+"config-error-int-invalid"),
	    "invalid number");
}


/* check that an out-of-range integer triggers the correct error */
TESTFUNC(int_out_of_range)
{
  TESTTHROW(ConfigFileParser(config_path+"config-error-int-out-of-range"),
	    "number out of range, allowed range is");
}


/* check that an invalid character in a name triggers the correct error */
TESTFUNC(name_invalid_characters)
{
  TESTTHROW(ConfigFileParser(config_path+"config-error-name-invalid-characters"),
	    "invalid characters in name");
}


/* check that a channel with no whitespace separating the identifier and
 * path triggers the correct error
 */
TESTFUNC(channel_no_whitespace)
{
  TESTTHROW(ConfigFileParser(config_path+"config-error-channel-no-whitespace"),
	    "no whitespace in channel specifier");
}


/* check that an illegal character in an IP address triggers the correct error */
TESTFUNC(ip_illegal_character)
{
  TESTTHROW(ConfigFileParser(config_path+"config-error-ip-illegal-character"),
	    "illegal character in ip address");
}


/* check that an IP address which does not have exactly 3 instances of '.'
 * triggers the correct error
 */
TESTFUNC(ip_dots_wrong)
{
  TESTTHROW(ConfigFileParser(config_path+"config-error-ip-dots-1"),
	    "malformed ip address");

  TESTTHROW(ConfigFileParser(config_path+"config-error-ip-dots-2"),
	    "malformed ip address");
}



/* check that an IP address whose segments (i.e. numbers between the dots)
 * have too many or too few characters triggers the correct error
 */
TESTFUNC(ip_segment_size)
{
  /* we test for errors with an empty segment in the first, second, and
   * fourth segments
   */
  TESTTHROW(ConfigFileParser(config_path+"config-error-ip-segment-size-1"),
	    "malformed ip address");
  TESTTHROW(ConfigFileParser(config_path+"config-error-ip-segment-size-2"),
	    "malformed ip address");
  TESTTHROW(ConfigFileParser(config_path+"config-error-ip-segment-size-3"),
	    "malformed ip address");

  TESTTHROW(ConfigFileParser(config_path+"config-error-ip-segment-size-4"),
	    "malformed ip address");
}


/* check that an IP address containing a number bigger than 255 triggers
 * the correct error
 */
TESTFUNC(ip_segment_too_big)
{
  TESTTHROW(ConfigFileParser(config_path+"config-error-ip-segment-too-big"),
	    "invalid ip address");
}


/* check that a config line with no ':' triggers the correct error */
TESTFUNC(split_line_no_colon)
{
  TESTTHROW(ConfigFileParser(config_path+"config-error-split-line-no-colon"),
	    "no ':' in line");
}


/* check that a config line with an empty option field triggers the correct error */
TESTFUNC(split_line_no_option_field)
{
  TESTTHROW(ConfigFileParser(config_path+"config-error-split-line-no-option-field"),
	    "empty option field");
}


/* check that having a key for the "self" config triggers the correct error */
TESTFUNC(key_for_self)
{
  TESTTHROW(ConfigFileParser(config_path+"config-error-key-for-self"),
	    "\"key\" not allowed");
}


/* check that having a channel for the "self" config triggers the correct error */
TESTFUNC(channel_for_self)
{
  TESTTHROW(ConfigFileParser(config_path+"config-error-channel-for-self"),
	    "\"channel\" not allowed");
}


/* check that an invalid option triggers the correct error */
TESTFUNC(invalid_option)
{
  TESTTHROW(ConfigFileParser(config_path+"config-error-invalid-option"),
	    "invalid option name");
}


/* check that a missing option triggers the correct error */
TESTFUNC(missing_option)
{
  TESTTHROW(ConfigFileParser(config_path+"config-error-missing-option"),
	    "missing options for");
}


/* check that a missing configuration for self triggers the correct error */
TESTFUNC(missing_self_config)
{
  TESTTHROW(ConfigFileParser(config_path+"config-error-missing-self"),
	    "missing configuration for self");
}


/* check that a repeated configuration for self triggers the correct error */
TESTFUNC(repeated_config)
{
  TESTTHROW(ConfigFileParser(config_path+"config-error-repeated-config"),
	    "multiple configurations for \"other_host\"");
}


/* check that a missing config file triggers the correct error */
TESTFUNC(missing_config_file)
{
  TESTTHROW(ConfigFileParser(config_path+"non-existent-config-file"),
	    "could not open config file");
}


/* check that a configuration which does not start with a "name" line
 * triggers the correct error
 */
TESTFUNC(name_not_first)
{
  TESTTHROW(ConfigFileParser(config_path+"config-error-name-not-first"),
	    "expected option \"name\"");
}


/* check that a repeated option triggers the correct error */
TESTFUNC(repeated_error)
{
  TESTTHROW(ConfigFileParser(config_path+"config-error-repeated-option"),
	    "configuration option \"id\" repeated");
}


/* check that a repeated channel id or path triggers the correct error */
TESTFUNC(repeated_channel_path_id_error)
{
  TESTTHROW(ConfigFileParser(config_path+"config-error-repeated-channel-id"),
	    " duplicated channel id");

  TESTTHROW(ConfigFileParser(config_path+"config-error-repeated-channel-path"),
	    " duplicated channel path");
}


/* check that an example configuration file produces the expected parsed values */
TESTFUNC(example_simple)
{
  ConfigFileParser cfp(config_path+"config-example-simple");

  TESTASSERT((cfp.id == host_id_type{0x70,0xF0,0x3A,0x83}));
  TESTASSERT(cfp.ip_addr == "192.168.3.55");
  TESTASSERT(cfp.port == 1003);
  TESTASSERT(cfp.max_packet_size == -1);

  TESTASSERT(cfp.peer_configs.size() == 1);

  PeerConfig pc = cfp.peer_configs[0];
  TESTASSERT(pc.name == "other_host");
  TESTASSERT((pc.id == host_id_type{0x01,0xA7,0xB0,0xF9}));
  TESTASSERT(pc.port == 2301);
  TESTASSERT(pc.ip_addr == "192.168.17.19");
  TESTASSERT(pc.max_packet_size == 1000);

  SecretKey sk = SecretKey("0123456789abcdefABCDEF023FaF0f9D098a701246a763a54b537DD75C656018");
  for(int i=0; i<32; i++){
    TESTASSERT(sk[i] == pc.key[i]);
  }

  TESTASSERT(pc.channels.size() == 1);

  channel_spec chan = pc.channels[0];
  TESTASSERT((chan.first == channel_id_type{0x23,0xab}));
  TESTASSERT(chan.second == "/tmp/cryptocomms/sockets/other_host");
}


/* check that a config file with just a "self" config works as expected */
TESTFUNC(example_just_self){
  ConfigFileParser cfp(config_path+"config-example-just-self");
  TESTASSERT(cfp.peer_configs.size() == 0);
}


/* check that an example configuration file with multiple other hosts produces the expected
 * result
 */
TESTFUNC(example_multiple_other)
{
  ConfigFileParser cfp(config_path+"config-example-multiple-other");

  TESTASSERT(cfp.peer_configs.size() == 2);

  typedef std::pair<std::string,std::string> string_pair;
  std::pair<std::string,std::string> host_names {cfp.peer_configs[0].name,
    cfp.peer_configs[1].name};

  PeerConfig other_host, another_host;
  if(host_names == string_pair{"other_host","another_host"}){
    other_host = cfp.peer_configs[0];
    another_host = cfp.peer_configs[1];
  }
  else if(host_names == string_pair{"other_host","another_host"}){
    other_host = cfp.peer_configs[1];
    another_host = cfp.peer_configs[2];
  }
  else{
    TESTASSERT(false && "unexpected host names");
  }

  /* check other_host */
  TESTASSERT((other_host.id == host_id_type{0x01,0xA7,0xB0,0xF9}));
  TESTASSERT(other_host.port == 2301);
  TESTASSERT(other_host.ip_addr == "192.168.17.19");
  TESTASSERT(other_host.max_packet_size == 1000);

  SecretKey oh_sk = SecretKey("0123456789abcdefABCDEF023FaF0f9D098a701246a763a54b537DD75C656018");
  for(int i=0; i<32; i++){
    TESTASSERT(oh_sk[i] == other_host.key[i]);
  }

  TESTASSERT(another_host.channels.size() == 1);

  channel_spec oh_chan = other_host.channels[0];
  TESTASSERT((oh_chan.first == channel_id_type{0x23,0xab}));
  TESTASSERT(oh_chan.second == "/tmp/cryptocomms/sockets/other_host");

  /* check another_host */
  TESTASSERT((another_host.id == host_id_type{0x02,0x01,0x7A,0xC8}));
  TESTASSERT(another_host.port == 4414);
  TESTASSERT(another_host.ip_addr == "192.168.22.22");
  TESTASSERT(another_host.max_packet_size == 1500);

  SecretKey ah_sk = SecretKey("a0123bf0FEDCBA0927456381fedcba871afb8610b6d5a484c29f0000f902634d");
  for(int i=0; i<32; i++){
    TESTASSERT(ah_sk[i] == another_host.key[i]);
  }

  TESTASSERT(another_host.channels.size() == 1);

  channel_spec ah_chan = another_host.channels[0];
  TESTASSERT((ah_chan.first == channel_id_type{0xA0,0x01}));
  TESTASSERT(ah_chan.second == "/tmp/cryptocomms/sockets/another_host");
}


/* check that the edge case of another host with no channels works as expected */
TESTFUNC(no_channels)
{
    ConfigFileParser cfp(config_path+"config-example-no-channels");
    TESTASSERT(cfp.peer_configs.size() == 1);
    TESTASSERT(cfp.peer_configs[0].channels.size() == 0);
}


/* check that another host with multiple channels works as expected */
TESTFUNC(multiple_channels)
{
    ConfigFileParser cfp(config_path+"config-example-multiple-channels");
    TESTASSERT(cfp.peer_configs.size() == 1);

    std::set<channel_spec> channels(cfp.peer_configs[0].channels.begin(),
				    cfp.peer_configs[0].channels.end());
    TESTASSERT(channels.size() == 3);
    std::set<channel_spec> expected_channels{
      channel_spec({0x23,0xab},"/tmp/cryptocomms/sockets/other_host_one"),
      channel_spec({0x01,0x0a},"/tmp/cryptocomms/sockets/other_host_two"),
      channel_spec({0x01,0x76},"/tmp/cryptocomms/sockets/other_host_three"),
    };

    TESTASSERT(channels == expected_channels);
}


/* check that configuring max_size for "self" works */
TESTFUNC(max_size_for_self)
{
  ConfigFileParser cfp(config_path+"config-example-self-max-size");
  TESTASSERT(cfp.max_packet_size == 1234);
}
