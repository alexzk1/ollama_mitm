#pragma once

#include <common/cm_ctors.h>
#include <network/ollama_proxy_config.hpp>
#include <ollama/httplib.h>
#include <ollama/json.hpp>
#include <ollama/ollama.hpp>

#include <cstddef>
#include <functional>

class CChunkedContentProvider
{
  public:
    CChunkedContentProvider() = delete;
    ~CChunkedContentProvider() = default;
    MOVEONLY_ALLOWED(CChunkedContentProvider);

    explicit CChunkedContentProvider(const httplib::Request &userRequest,
                                     const TOllamaProxyConfig &proxyConfig);
    bool operator()(std::size_t offset, httplib::DataSink &sink);

  private:
    Ollama ollamaServer;
    nlohmann::json parsedUserJson;
    std::reference_wrapper<const httplib::Request> userRequest;
    std::reference_wrapper<const TOllamaProxyConfig> proxyConfig;
};
