#pragma once

#include "http_headers.hpp" // IWYU pragma: keep
#include "socket.hpp"       // IWYU pragma: keep

#include <common/cm_ctors.h>

class CHttpSocket
{
  public:
    explicit CHttpSocket(CClientSocket aSocket);
    ~CHttpSocket() = default;
    MOVEONLY_ALLOWED(CHttpSocket);

    void ReadAll();

  private:
    CClientSocket iSocket;
    HttpHeaders iHeaders;
};
