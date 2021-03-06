#ifndef CONFIGSERVER_H
#define CONFIGSERVER_H

// STL
#include <memory>
#include <list>
#include <string>
#include <unordered_map>

// PUT
#include <put/socket.h>
#include <put/cxxutils/posix_helpers.h>
#include <put/cxxutils/vfifo.h>
#include <put/cxxutils/hashing.h>
#include <put/cxxutils/configmanip.h>
#include <put/specialized/fileevent.h>

class ConfigServer : public ServerSocket
{
public:
  ConfigServer(void) noexcept;
 ~ConfigServer(void) noexcept;

private:
  bool valueSet   (const posix::fd_t socket, const std::string& key, const std::string& value) const noexcept;
  bool valueUnset (const posix::fd_t socket, const std::string& key) const noexcept;
  bool syncReturn (const posix::fd_t socket, const posix::error_t errcode) const noexcept;
  bool unsetReturn(const posix::fd_t socket, const posix::error_t errcode, const std::string& key) const noexcept;
  bool setReturn  (const posix::fd_t socket, const posix::error_t errcode, const std::string& key) const noexcept;
  bool getReturn  (const posix::fd_t socket, const posix::error_t errcode, const std::string& key, const std::string& value, const std::list<std::string>& children) const noexcept;

  void syncCall   (posix::fd_t socket) noexcept;
  void unsetCall  (posix::fd_t socket, const std::string& key) noexcept;
  void setCall    (posix::fd_t socket, const std::string& key, const std::string& value) noexcept;
  void getCall    (posix::fd_t socket, const std::string& key) noexcept;

  bool peerChooser(posix::fd_t socket, const proccred_t& cred) noexcept;
  void receive(posix::fd_t socket, vfifo buffer, posix::fd_t fd) noexcept;
  void request(posix::fd_t socket, posix::sockaddr_t addr, proccred_t cred) noexcept;

  void removePeer(posix::fd_t socket) noexcept;
  void fileUpdated(std::string filename, FileEvent::Flags_t flags) noexcept;

  struct configfile_t
  {
    std::unique_ptr<FileEvent> fevent;
    ConfigManip config;
  };
  std::unordered_map<pid_t, posix::fd_t> m_endpoints;
  std::unordered_map<posix::fd_t, configfile_t> m_configfiles;
};

inline bool ConfigServer::valueSet(const posix::fd_t socket, const std::string& key, const std::string& value) const noexcept
  { return write(socket, vfifo("RPC", "valueSet", key, value), posix::invalid_descriptor); }

inline bool ConfigServer::valueUnset(const posix::fd_t socket, const std::string& key) const noexcept
  { return write(socket, vfifo("RPC", "valueUnset", key), posix::invalid_descriptor); }

inline bool ConfigServer::syncReturn(const posix::fd_t socket, const posix::error_t errcode) const noexcept
  { return write(socket, vfifo("RPC", "syncReturn", errcode), posix::invalid_descriptor); }

inline bool ConfigServer::unsetReturn(const posix::fd_t socket, const posix::error_t errcode, const std::string& key) const noexcept
  { return write(socket, vfifo("RPC", "unsetReturn", errcode, key), posix::invalid_descriptor); }

inline bool ConfigServer::setReturn(const posix::fd_t socket, const posix::error_t errcode, const std::string& key) const noexcept
  { return write(socket, vfifo("RPC", "setReturn", errcode, key), posix::invalid_descriptor); }

inline bool ConfigServer::getReturn(const posix::fd_t socket, const posix::error_t errcode, const std::string& /*key*/,
                                    const std::string& value, const std::list<std::string>& children) const noexcept
  { return write(socket, vfifo("RPC", "getReturn", errcode, value, children), posix::invalid_descriptor); }

#endif // CONFIGSERVER_H
