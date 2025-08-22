#include "ConfigFileParser.h"

#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <iterator>
#include <cctype>
#include <set>


namespace
{
  const std::string self_name = "self";

  /* not_isspace() is a simple predicate to be passed to algorithms */
  bool not_isspace(const unsigned char ch)
  { return !std::isspace(ch); }


  /* ltrim() and rtrim() perform in-place trimming of whitespace from strings */
  void ltrim(std::string &s)
  {
    auto non_whitespace_begin = std::find_if(s.begin(),s.end(),not_isspace);
    s.erase(s.begin(),non_whitespace_begin);
  }

  void rtrim(std::string &s)
  {
    auto non_whitespace_end = std::find_if(s.rbegin(),s.rend(),not_isspace).base();
    s.erase(non_whitespace_end,s.end());
  }


  /* erase_string() sets the internal memory of a std::string to zero. This is important
   * if the string could have held the hexadecimal representation of a secret key.
   */
  void erase_string(std::string& str)
  { for(char& c : str){ c = 0; } }


  /* These strings are used with check_string_chars() to validate the content of strings
   * which represent certain types of object.
   */
  const std::string allowed_name_chars =
    "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ-_";
  const std::string allowed_hex_chars  = "0123456789abcdefABCDEF";
  const std::string allowed_ip_chars = "0123456789.";


  /* check_string_chars() checks that all of the characters in str occur in good_chars */
  bool check_string_chars(const std::string& str,const std::string& good_chars)
  {
    auto check_func = [&](char c){return (good_chars.find(c) != std::string::npos);};
    return std::all_of(str.begin(),str.end(),check_func);
  }


  /* ConfigLineError represents an error arising from a mistake in some particular line
   * in the config file. These errors are thrown by functions called from parse_peer_config()
   * (directly or indirectly), and they are caught in parse_peer_config(), where their messages
   * are annotated with the line number and then thrown again.
   */
  class ConfigLineError: public std::runtime_error
  {
  public:
    ConfigLineError(const std::string& msg):
      runtime_error(msg) {}
  };


  /* parse_hex_string() converts a string of hexadecimal characters to a std::array of
   * unsigned char whose bytes have the values specified in the string. Each pair of
   * characters in str is read as a hexadecimal value in the range 0...255 and the
   * corresponding element of the std::array is set to this value. Note that this
   * function needs to be implemented as a template over the length of the std::array,
   * because this length is a template parameter to std::array.
   */
  template <int N>
  std::array<unsigned char,N> parse_hex_string(const std::string& str){
    if(str.size() != 2*N){
      throw ConfigLineError("string is the wrong length");
    }

    if(not check_string_chars(str,allowed_hex_chars)){
      throw ConfigLineError("invalid characters present");
    }

    std::array<unsigned char,N> bytes_from_hex;
    for(int i=0; i<N; i++){
      bytes_from_hex[i] = std::stoi(str.substr(i*2,2),nullptr,16);
    }

    return bytes_from_hex;
  }


  /* parse_integer() parses str to an integer using std::stoi, throwing an error if
   * std::stoi throws an error, and also if std::stoi does not consume the whole
   * string or if the resulting integer str_int does not satisfy
   * least <= str_int <= greatest
   */
  int parse_integer(const std::string& str, const int least, const int greatest)
  {
    std::size_t pos;
    int str_int = std::stoi(str,&pos);

    if(pos < str.size()){
      throw ConfigLineError("invalid number");
    }

    if( (str_int < least) or (str_int > greatest) ){
      throw ConfigLineError(
			    "number out of range, allowed range is ("
			    +std::to_string(least)+","+std::to_string(greatest)+")");
    }

    return str_int;
  }


  /* parse_name() checks that value_string is a valid peer name, throwing an error
   * if not and returning value_string if so
   */
  std::string parse_name(const std::string& value_string)
  {
    if(not check_string_chars(value_string,allowed_name_chars)){
      throw ConfigLineError("invalid characters in name: "+value_string);
    }
    return value_string;
  }


  /* parse_id() parses a host id. Host ids are 4 byte integers, which are
   * represented in a config file as a string of 8 hexadecimal digits
   */
  std::array<unsigned char,4> parse_id(const std::string& value_string)
  {
    std::array<unsigned char,4> id;
    try{
      id = parse_hex_string<4>(value_string);
    }
    catch(ConfigLineError& e){
      throw ConfigLineError(std::string("error parsing id, ")+e.what());
    }

    return id;
  }


  /* parse_channel() parses a channel description. A channel description consists
   * of a two byte channel id and a filesystem path (representing the location of the
   * endpoint of this channel on the local machine). In the config file, a channel
   * description is represented as a string of four hex digits, followed by some
   * whitespace, followed by the filesystem path (which may itself contain whitespace).
   * Including the "channel: " prefix, this means that a channel description line might
   * look like this
   * channel: 01a4 /root/dir1/dir2/blah
   * Note that ConfigFileParser does not do any validation of the filesystem path, and will
   * thus accept any string which begins with a non-whitespace character
   */
  channel_spec parse_channel(const std::string& value_string)
  {
    auto first_chunk_end = std::find_if(value_string.begin(),value_string.end(),isspace);
    if(first_chunk_end == value_string.end()){
      throw ConfigLineError("no whitespace in channel specifier");
    }

    auto second_chunk_start = std::find_if(first_chunk_end,value_string.end(),not_isspace);
    if(second_chunk_start == value_string.end()){
      /* This error should never occur, since value_string should have no trailing whitespace,
       * so a missing path should trigger the previous "no whitespace" error. However, sanity
       * checks are good.
       */
      throw ConfigLineError("no path in channel specifier");
    }

    auto channel_id = std::string(value_string.begin(),first_chunk_end);
    auto channel_path = std::string(second_chunk_start,value_string.end());

    channel_spec channel;
    try{
      channel = channel_spec{parse_hex_string<2>(channel_id),channel_path};
    }
    catch(ConfigLineError& e){
      throw ConfigLineError(std::string("error parsing channel id, ")+e.what());
    }

    return channel;
  }


  /* parse_ip() validates that value_string is a validly formatted IPv4 address,
   * i.e. that it consists of four integers in the range 0-255 separated by three
   * period characters. If this is the case, validate_ip() returns value_string,
   * and if not then an error is thrown.
   */
  std::string parse_ip(const std::string& value_string)
  {
    if(not check_string_chars(value_string,allowed_ip_chars)){
      throw ConfigLineError("illegal character in ip address");
    }

    if(std::count(value_string.begin(),value_string.end(),'.') != 3){
      throw ConfigLineError("malformed ip address");
    }

    /* at this point, we know that the string contains exactly three periods,
     * and that any other characters which it contains must consist exclusively
     * of the digits 0123456789
     */

    auto it = value_string.begin();
    while(it != value_string.end()){
      auto next_period = std::find(it,value_string.end(),'.');
      std::string chunk(it,next_period);

      if( (chunk.size() < 1) or (chunk.size() > 3) ){
	throw ConfigLineError("malformed ip address");
      }

      int byte_value = std::stoi(chunk);
      if(byte_value > 255){
	throw ConfigLineError("invalid ip address");
      }

      if(next_period == value_string.end()){
	it = value_string.end();
      }
      else{
	it = next_period + 1;
	if(it == value_string.end()){
	  // the last character of value_string is a '.', error
	  throw ConfigLineError("malformed ip address");
	}
      }

    }

    return value_string;
  }


  /* parse_port() parses value_string into a port number
   */
  int parse_port(const std::string& value_string)
  {
    int port;
    try{
      port = parse_integer(value_string,0,65535); // 65535 is the maximum UDP port number
    }
    catch(ConfigLineError& e){
      throw ConfigLineError(std::string("invalid port number, ")+e.what());
    }

    return port;
  }


  /* parse_max_size() parses value_string into an integer representing the maximum
   * size (in bytes) of data payload to be sent in a UDP packet
   */
  int parse_max_size(const std::string& value_string)
  {
    int max_size;
    try{
      /* max_size represents the maximum payload we will send in a UDP packet. The
       * maximum possible payload size for UDP over IPv4 is 65507 bytes, and so
       * we do not allow max_size to exceed this value.
       */
      max_size = parse_integer(value_string,0,65507);
    }
    catch(ConfigLineError& e){
      throw ConfigLineError(std::string("invalid max_size, ")+e.what());
    }

    return max_size;
  }


  /* split_config_line() splits a config file line into an option name and an option
   * value. The split is made at the first colon which occurs in the line, and both
   * parts of the line are trimmed of whitespace.
   */
  std::pair<std::string,std::string> split_config_line(const std::string& config_line)
  {
    auto colon_pos = config_line.find(':');
    if(colon_pos == std::string::npos){
      throw ConfigLineError("no ':' in line");
    }

    std::string first_part = config_line.substr(0,colon_pos);
    rtrim(first_part);
    ltrim(first_part);
    if(first_part == ""){
      throw ConfigLineError("empty option field");
    }

    std::string second_part = config_line.substr(colon_pos+1);
    rtrim(second_part);
    ltrim(second_part);

    return std::pair<std::string,std::string>(first_part,second_part);
  }


  /* ParseState represents the state of a parsing pass through a config file. A parse
   * state object is created by the ConfigFileParser constructor, and this object is
   * then passed into calls to parse_peer_config() to track the progress of the parse.
   *
   * ParseState reads the whole config file line-by-line in its constructor, and stores
   * these lines.
   */
  struct ParseState
  {
    ParseState(const std::string& path);
    std::vector<std::string> lines;
    std::vector<std::string>::iterator pos;
    ~ParseState();

    /* we do not allow any copying or moving of a ParseState object */
    ParseState(const ParseState&) = delete;
    ParseState(ParseState&&) = delete;
    ParseState& operator=(const ParseState&) = delete;
    ParseState& operator=(ParseState&&) = delete;
  };


  /* The ParseState constructor reads in all of the lines of the file specified by
   * the "path" argument, and stores them in the "lines" member.
   */
  ParseState::ParseState(const std::string& path)
  {
    std::ifstream file_stream(path);
    if(!file_stream){
      throw std::runtime_error("ConfigFileParser: could not open config file: "+path);
    }

    std::string file_line;
    while(std::getline(file_stream,file_line)){
      lines.push_back(file_line);
      erase_string(file_line);
    }

    if(not file_stream.eof()){
      throw std::runtime_error("ConfigFileParser: error reading file: "+path);
    }

    pos = lines.begin();
  }


  /* ParseState's destructor zeros out all of the stored lines, ensuring that the
   * lines which hold secret keys (in the form of hexadecimal strings) cannot be
   * leaked via memory reuse.
   */
  ParseState::~ParseState()
  { for(std::string& line : lines){ erase_string(line); } }


  /* check_required_options() is a simple convenience function which compares its two
   * arguments and returns a set containing any elements of required_opts which are
   * missing from seen_opts.
   */
  std::set<std::string> check_required_options(const std::set<std::string>& required_opts,
					       const std::set<std::string>& seen_opts)
  {
    std::set<std::string> missing_opts;
    std::copy_if(required_opts.begin(),required_opts.end(),
		 std::inserter(missing_opts,missing_opts.begin()),
		 [&](const std::string& s){ return seen_opts.count(s) == 0; }
		 );
    return missing_opts;
  }


  /* config_line_error() is a simple convenience function for creating and
   * throwing an error which arises from some problem with the config file,
   * including the number of the line which caused the error.
   */
  void config_line_error(const std::string& err_msg, const int line_num)
  {
    std::string err_str = "ConfigFileParser: [line "+ std::to_string(line_num)
      + "] " + err_msg;
    throw std::runtime_error(err_str);
  }


  /* parse_peer_config() parses the next peer configuration from parse_state
   * and stores it in peer_config. Each run of parse_peer_config() reads one
   * "configuration block", which consists of multiple lines specifying options.
   * A configuration block must begin with a line specifying the option "name",
   * and it ends when either the next "name" line or end-of-file is encountered.
   * Some options are mandatory, others optional, and apart from the "channel"
   * option, no option my occur more than once in a configuration block.
   *
   * parse_peer_config() returns false to signal that all lines have been consumed,
   * and true otherwise.
   */
  bool parse_peer_config(PeerConfig& peer_config, ParseState& parse_state)
  {
    if(parse_state.pos == parse_state.lines.end()){
      return false;
    }

    /* clear out any values in peer_config */
    peer_config.clear();

    /* Parse the configuration line-by-line, recording each option name we see.
     * Recording which option names we have seen serves three purposes. One, we
     * can detect when an option is repeated and signal an error (except for
     * the option "channel", which can occur multiple times). Two, we can ensure
     * that the first option given is "name". Thirdly, we can use the list of seen
     * option names to check that all required options were present.
     */
    std::set<std::string> option_names_seen;
    while(parse_state.pos != parse_state.lines.end()){
      std::string line = *(parse_state.pos);
      parse_state.pos++;

      /* Ignore lines which consist only of whitespace, as well as comment lines.
       * A line is considered a comment if its first non-whitespace character is #
       */
      auto content_begin = std::find_if(line.begin(),line.end(),not_isspace);
      if((content_begin == line.end()) or (*content_begin == '#')){
	continue;
      }

      /* split the line into an option name and an option value*/
      std::pair<std::string,std::string> line_pair = split_config_line(line);
      std::string option_name = line_pair.first;
      std::string option_value = line_pair.second;
      erase_string(line);

      /* if we've already seen a "name" line and this line is a "name" line, then
       * this is the start of the next configuration block, so we are done
       */
      if( (option_name == "name") and (option_names_seen.count(option_name) != 0) ){
	parse_state.pos--;
	break;
      }

      auto line_num = std::distance(parse_state.lines.begin(),parse_state.pos);

      /* ensure that the first line in any configuration block is a "name" line  */
      if( (option_name != "name") and (option_names_seen.count("name") == 0) ){
	config_line_error("expected option \"name\"",line_num);
      }

      /* forbid multiple occurrences of any option except "channel" */
      if( (option_names_seen.count(option_name) != 0) and (option_name != "channel") ){
	config_line_error("configuration option \""+option_name+"\" repeated",line_num);
      }

      try{
	/* The extended if-else below functions as a switch statement on the string
	 * option_name. The functions called below can throw ConfigLineError, which
	 * represents an error arising from an incorrect configuration line. The enclosing
	 * try-catch block catches these errors and uses config_line_error to propagate
	 * this error with the line number attached.
	 */

	if(option_name == "name")
	  peer_config.name = parse_name(option_value);

	else if(option_name == "id")
	  peer_config.id = parse_id(option_value);

	else if( (option_name == "key") and (peer_config.name != self_name) )
	  peer_config.key = SecretKey(option_value);

	else if( (option_name == "key") and (peer_config.name == self_name) )
	  throw ConfigLineError("\"key\" not allowed for \""+self_name+"\"");

	else if( (option_name == "channel") and (peer_config.name != self_name) )
	  peer_config.channels.push_back(parse_channel(option_value));

	else if( (option_name == "channel") and (peer_config.name == self_name) )
	  throw ConfigLineError("\"channel\" not allowed for \""+self_name+"\"");

	else if(option_name == "ip")
	  peer_config.ip_addr = parse_ip(option_value);

	else if(option_name == "port")
	  peer_config.port = parse_port(option_value);

	else if(option_name == "max_size")
	  peer_config.max_packet_size = parse_max_size(option_value);

	else
	  throw ConfigLineError("invalid option name \""+option_name+"\"");

      }
      catch(ConfigLineError& e){
	config_line_error(e.what(),line_num);
      }


      option_names_seen.insert(option_name);

      /* we erase the string in case it holds a secret key in hexadecimal form */
      erase_string(option_value);
    }

    /* check that all required options have been given */
    std::set<std::string> required_options = (peer_config.name == self_name) ?
      std::set<std::string>{"id","ip","port"} : std::set<std::string>{"id","ip","port","key"};
    std::set<std::string> missing_options = check_required_options(required_options,
								   option_names_seen);
    if(missing_options != std::set<std::string>{}){
      std::string missing_options_string;
      for(auto s : missing_options){
	missing_options_string += " "+s;
      }
      throw std::runtime_error("ConfigFileParser: missing options for \""+peer_config.name+"\"\n  "+
			       missing_options_string);
    }

    /* check that no channel id has been repeated */
    std::multiset<std::array<unsigned char,2>> channel_ids;
    std::transform(peer_config.channels.begin(), peer_config.channels.end(),
		   std::inserter(channel_ids,channel_ids.end()),
		   [](channel_spec& cs){return cs.first;}
		   );
    for(auto& x : channel_ids){
      if(channel_ids.count(x) > 1){
	throw std::runtime_error("ConfigFileParser: duplicated channel id for \""
				 +peer_config.name+"\"\n  "
				 );
      }
    }

    /* check that no channel path has been repeated */
    std::multiset<std::string> channel_paths;
    std::transform(peer_config.channels.begin(), peer_config.channels.end(),
		   std::inserter(channel_paths,channel_paths.end()),
		   [](channel_spec& cs){return cs.second;}
		   );
    for(auto& x : channel_paths){
      if(channel_paths.count(x) > 1){
	throw std::runtime_error("ConfigFileParser: duplicated channel path for \""
				 +peer_config.name+"\"\n  "
				 );
      }
    }


    return true;
  }

}


/* ConfigFileParser::ConfigFileParser() reads the configuration in the file specified
 * by the "path" argument, and constructs the ConfigFileParser from this.
 */
ConfigFileParser::ConfigFileParser(const std::string& path)
{
  ParseState parse_state(path);
  PeerConfig peer_config;
  std::set<std::string> config_names_seen;
  while(parse_peer_config(peer_config,parse_state)){

    if(config_names_seen.count(peer_config.name) != 0){
      throw std::runtime_error("ConfigFileParser: multiple configurations for \""+peer_config.name+"\"");
    }
    config_names_seen.insert(peer_config.name);

    if(peer_config.name == self_name){
      id = peer_config.id;
      ip_addr = peer_config.ip_addr;
      port = peer_config.port;
      /* note that parse_peer_config sets peer_config.max_packet_size to -1 (via PeerConfig.clear() )
       * if no maximum packet size is given in the config file
       */
      max_packet_size = peer_config.max_packet_size;
    }
    else{
      peer_configs.push_back(peer_config);
    }

  }

  if(config_names_seen.count(self_name) == 0){
    throw std::runtime_error("ConfigFileParser: missing configuration for "+self_name);
  }
}
