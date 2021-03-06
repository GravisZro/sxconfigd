﻿#include "directorconfigserver.h"

// POSIX
#include <dirent.h>

// STL
#include <algorithm>

// PUT
#include <put/cxxutils/syslogstream.h>

#ifndef DIRECTOR_CONFIG_PATH
#define DIRECTOR_CONFIG_PATH  "/etc/director"
#endif

#ifndef DIRECTOR_USERNAME
#define DIRECTOR_USERNAME     "director"
#endif
/*
static std::string extract_provider_name(const std::string& filename)
{
  posix::size_t start = filename.rfind('/');
  posix::size_t end   = filename.rfind('.');

  if(start == std::string::npos ||
     end   == std::string::npos)
    return std::string();
  return filename.substr(start, end - start);
}
*/
static const char* extract_provider_name(const char* filename)
{
  char provider[NAME_MAX];
  const char* start = posix::strrchr(filename, '/');
  const char* end   = posix::strrchr(filename, '.');

  if(start == NULL || // if '/' NOT found OR
     end   == NULL || // '.' found AND
     end < start || // occur in the incorrect order OR
     posix::strcmp(end, ".conf")) // doesn't end with ".conf"
    return nullptr;
  return posix::strncpy(provider, start + 1, posix::size_t(end - start + 1)); // extract provider name
}

static const char* director_configfilename(const char* filename)
{
  // construct config filename
  static char fullpath[PATH_MAX];
  posix::memset(fullpath, 0, PATH_MAX);
  if(posix::snprintf(fullpath, PATH_MAX, "%s/%s", DIRECTOR_CONFIG_PATH, filename) == posix::error_response) // I don't how this could fail
    return nullptr; // unable to build config filename
  return fullpath;
}

static bool readconfig(const char* name, std::string& buffer)
{
  posix::FILE* file = posix::fopen(name, "a+b");

  if(file == NULL)
  {
    posix::syslog << posix::priority::warning
                  << "Unable to open file: %1 : %2"
                  << name
                  << posix::strerror(errno)
                  << posix::eom;
    return false;
  }

  buffer.clear();
  buffer.resize(posix::size_t(posix::ftell(file)), '\n');
  if(buffer.size())
  {
    posix::rewind(file);
    posix::fread(const_cast<char*>(buffer.data()), sizeof(std::string::value_type), buffer.size(), file);
  }
  posix::fclose(file);
  return true;
}


DirectorConfigServer::DirectorConfigServer(void) noexcept
{
  std::string buffer;
  DIR* dir = ::opendir(DIRECTOR_CONFIG_PATH);
  dirent* entry = NULL;
  const char* provider = nullptr;
  const char* filename = nullptr;
  if(dir != NULL)
  {
    while((entry = ::readdir(dir)) != NULL)
    {
      if(entry->d_name[0] == '.') // if dot files/dirs
        continue; // skip file

      if((provider = extract_provider_name  (entry->d_name)) == nullptr || // if provider name extraction failed OR
         (filename = director_configfilename(entry->d_name)) == nullptr) // failed to build filename
        continue; // skip file

      if(readconfig(filename, buffer)) // able to read config file
      {
        auto& conffile = m_configfiles[provider];
        conffile.fevent = std::make_unique<FileEvent>(filename, FileEvent::WriteEvent);
        conffile.config.clear(); // erase any existing data
        conffile.config.importText(buffer);
        Object::connect(conffile.fevent->activated, this, &DirectorConfigServer::fileUpdated);
      }
    }
    ::closedir(dir);
  }
/*
  m_dir = EventBackend::watch(DIRECTOR_CONFIG_PATH, EventFlags::DirEvent);
  if(m_dir > 0)
    Object::connect(m_dir, this, &DirectorConfigServer::dirUpdated);
*/
  Object::connect(newPeerRequest  , this, &DirectorConfigServer::request);
  Object::connect(newPeerMessage  , this, &DirectorConfigServer::receive);
  Object::connect(disconnectedPeer, this, &DirectorConfigServer::removePeer);
}

DirectorConfigServer::~DirectorConfigServer(void) noexcept
{
}

void DirectorConfigServer::fileUpdated(std::string filename, FileEvent::Flags_t flags) noexcept
{
  const char* provider = nullptr;
  if(flags.WriteEvent &&
     (provider = extract_provider_name(filename.c_str())) != nullptr) // extracted provider name
    for(auto& confpair : m_configfiles)
      if(confpair.second.fevent->file() == provider)
      {
        std::string tmp_buffer;
        std::unordered_map<std::string, std::string> old_config, new_config;

        confpair.second.config.exportKeyPairs(old_config); // export data
        confpair.second.config.clear(); // wipe config

        if(readconfig(filename.c_str(), tmp_buffer) &&
           confpair.second.config.importText(tmp_buffer))
        {
          confpair.second.config.exportKeyPairs(new_config);

          for(auto& old_pair : old_config) // find removed and updated values
          {
            auto iter = new_config.find(old_pair.first);
            if(iter == new_config.end())
              for(auto& endpoint : m_endpoints)
                valueUnset(endpoint.second, confpair.first, old_pair.first); // invoke value deletion
            else if(iter->second != old_pair.second)
              for(auto& endpoint : m_endpoints)
                valueSet(endpoint.second, confpair.first, iter->first, iter->second); // invoke value update
          }

          for(auto& new_pair : new_config) // find completely new values
            if(old_config.find(new_pair.first) == old_config.end()) // if old config doesn't have a new config key
              for(auto& endpoint : m_endpoints)
                valueSet(endpoint.second, confpair.first, new_pair.first, new_pair.second); // invoke value update
        }
        else
          posix::syslog << posix::priority::warning
                        << "Failed to read/parse config file: %1"
                        << filename
                        << posix::eom;
      }
}

void DirectorConfigServer::dirUpdated(std::string dirname, FileEvent::Flags_t flags) noexcept
{
  posix::printf("dir updated: %s - 0x%02x\n", dirname.c_str(), uint8_t(flags));
}

void DirectorConfigServer::listConfigsCall(posix::fd_t socket) noexcept
{
  std::vector<std::string> names;
  for(const auto& confpair : m_configfiles)
    names.push_back(confpair.first);
  listConfigsReturn(socket, names);
}

void DirectorConfigServer::syncCall(posix::fd_t socket) noexcept
{
  bool ok = true;
  for(const auto& confpair : m_configfiles) // for each parsed config file
  {
    std::unordered_map<std::string, std::string> data;
    confpair.second.config.exportKeyPairs(data); // export config data
    for(auto& pair : data) // for each key pair
      ok &= valueSet(socket, confpair.first, pair.first, pair.second); // send value
  }
  syncReturn(socket, ok ? posix::success_response : errno); // send call response
}

void DirectorConfigServer::setCall(posix::fd_t socket, const std::string& config, const std::string& key, const std::string& value) noexcept
{
  posix::error_t errcode = posix::success_response;

  auto configfile = m_configfiles.find(config);
  if(configfile == m_configfiles.end())
    errcode = posix::error_t(posix::errc::invalid_argument); // not a valid config file name
  else
    configfile->second.config.getNode(key)->value = value;

  setReturn(socket, errcode, config, key);
}

void DirectorConfigServer::getCall(posix::fd_t socket, const std::string& config, const std::string& key) noexcept
{
  std::vector<std::string> children;
  std::string value;
  posix::error_t errcode = posix::success_response;

  auto configfile = m_configfiles.find(config); // look up config by name
  if(configfile == m_configfiles.end()) // if not found
    errcode = posix::error_t(posix::errc::invalid_argument); // not a valid config file name
  else
  {
    auto node = configfile->second.config.findNode(key); // find node in config file
    if(node == nullptr)
      errcode = posix::error_t(posix::errc::invalid_argument); // doesn't exist
    else
    {
      switch(node->type)
      {
        case node_t::type_e::array:
        case node_t::type_e::multisection:
        case node_t::type_e::section:
          for(const auto& child : node->children)
            children.push_back(child.first);
        case node_t::type_e::invalid:
        case node_t::type_e::value:
        case node_t::type_e::string:
          value = node->value;
      }
    }
  }
  getReturn(socket, errcode, config, key, value, children);
}

void DirectorConfigServer::unsetCall(posix::fd_t socket, const std::string& config, const std::string& key) noexcept
{
  posix::error_t errcode = posix::success_response;
  auto configfile = m_configfiles.find(config); // look up config by name

  if(configfile == m_configfiles.end())
    errcode = posix::error_t(posix::errc::io_error); // no such config file!
  else if(!configfile->second.config.deleteNode(key))
    errcode = posix::error_t(posix::errc::invalid_argument); // doesn't exist

  unsetReturn(socket, errcode, config, key);
}

bool DirectorConfigServer::peerChooser(posix::fd_t socket, const proccred_t& cred) noexcept
{
  if(posix::strcmp(DIRECTOR_USERNAME, posix::getusername(cred.uid))) // username must be "director"
    return false; // didn't match, reject connection

  auto endpoint = m_endpoints.find(cred.pid);
  if(endpoint == m_endpoints.end() || // if no connection exists OR
     !peerData(endpoint->second))     // if old connection is mysteriously gone (can this happen?)
  {
    m_endpoints[cred.pid] = socket; // insert or assign new value
    return true;
  }
  return false; // reject multiple connections from one endpoint
}

void DirectorConfigServer::removePeer(posix::fd_t socket) noexcept
{
  for(auto endpoint : m_endpoints)
    if(socket == endpoint.second)
      { m_endpoints.erase(endpoint.first); break; }
}

void DirectorConfigServer::request(posix::fd_t socket, posix::sockaddr_t addr, proccred_t cred) noexcept
{
  (void)addr;
  if(peerChooser(socket, cred))
    acceptPeerRequest(socket);
  else
    rejectPeerRequest(socket);
}

void DirectorConfigServer::receive(posix::fd_t socket, vfifo buffer, posix::fd_t fd) noexcept
{
  (void)fd;
  std::string config, key, value;
  if(!(buffer >> value).hadError() && value == "RPC" &&
     !(buffer >> value).hadError())
  {
    switch(hash(value))
    {
      case "listConfigsCall"_hash:
        listConfigsCall(socket);
        break;
      case "syncCall"_hash:
        syncCall(socket);
        break;
      case "setCall"_hash:
        buffer >> config >> key >> value;
        if(!buffer.hadError())
          setCall(socket, config, key, value);
        break;
      case "getCall"_hash:
        buffer >> config >> key;
        if(!buffer.hadError())
          getCall(socket, config, key);
        break;
      case "unsetCall"_hash:
        buffer >> config >> key;
        if(!buffer.hadError())
          unsetCall(socket, config, key);
        break;
    }
  }
}
