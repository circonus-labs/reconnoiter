/*
 * Copyright (c) 2023, Circonus, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name Circonnus, Inc. nor the names
 *       of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written
 *       permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

#include "otlp.hpp"

#include <grpcpp/grpcpp.h>
#include "opentelemetry/proto/collector/metrics/v1/metrics_service.grpc.pb.h"

class GRPCService final: public OtelCollectorMetrics::MetricsService::Service
{
  grpc::Status Export(grpc::ServerContext* context,
                      const OtelCollectorMetrics::ExportMetricsServiceRequest* request,
                      OtelCollectorMetrics::ExportMetricsServiceResponse* response) override;
};

GRPCService grpcservice;
static std::unique_ptr<std::thread> grpcserver;

struct otlpgrpc_mod_config : otlp_mod_config {
  std::string grpc_server;
  int grpc_port;
  bool use_grpc_ssl;
  bool grpc_ssl_use_broker_cert;
  bool grpc_ssl_use_root_cert;
};

otlp_mod_config *make_new_mod_config()
{
  return new otlpgrpc_mod_config;
}

static std::string read_keycert(const std::string filename)
{
  std::ifstream file(filename, std::ios::binary);
  std::stringstream buffer;
  buffer << file.rdbuf();
  file.close();
  return buffer.str();
}

static void grpc_server_thread(std::string server_address,
                               bool use_grpc_ssl,
                               bool grpc_ssl_use_broker_cert,
                               bool grpc_ssl_use_root_cert,
                               std::string broker_crt,
                               std::string broker_key,
                               std::string root_crt)
{
  grpc::ServerBuilder builder;
  std::shared_ptr<grpc::ServerCredentials> server_creds;
  if (grpc_ssl_use_broker_cert) {
    mtevL(nldeb, "[otlpgrpc] setting up grpc ssl using broker cert and key\n");
    // read the cert and key
    std::string servercert = read_keycert(broker_crt);
    std::string serverkey = read_keycert(broker_key);

    // create a pem key cert pair using the cert and the key
    grpc::SslServerCredentialsOptions::PemKeyCertPair pkcp;
    pkcp.private_key = serverkey;
    pkcp.cert_chain = servercert;

    // alter the server ssl opts to put in our own cert/key and optionally the root cert too
    grpc::SslServerCredentialsOptions ssl_opts;
    ssl_opts.pem_root_certs="";
    if (grpc_ssl_use_root_cert) {
      mtevL(nldeb, "[otlpgrpc] registering grpc ssl root cert\n");
      std::string rootcert = read_keycert(root_crt);
      ssl_opts.pem_root_certs = rootcert;
    }
    ssl_opts.pem_key_cert_pairs.push_back(pkcp);

    // create a server credentials object to use on the listening port
    server_creds = grpc::SslServerCredentials(ssl_opts);
  }
  else {
    server_creds = grpc::SslServerCredentials(grpc::SslServerCredentialsOptions());
  }
  if (!use_grpc_ssl) {
    server_creds = grpc::InsecureServerCredentials();
  }
  builder.AddListeningPort(server_address, server_creds);
  builder.RegisterService(&grpcservice);
  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  mtevL(nldeb, "[otlpgrpc] grpc server listening on %s\n", server_address.c_str());
  server->Wait();
  mtevL(nlerr, "[otlpgrpc] gprc server terminated!\n"); // should not happen normally
}

grpc::Status GRPCService::Export(grpc::ServerContext* context,
                                 const OtelCollectorMetrics::ExportMetricsServiceRequest* request,
                                 OtelCollectorMetrics::ExportMetricsServiceResponse* response)
{
  constexpr auto handle_error = [](const std::string &error) {
    mtevL(nldeb_verbose, "[otlpgrpc] grpc metric data batch error: %s\n", error.c_str());
    return grpc::Status(grpc::StatusCode::NOT_FOUND, error);
  };

  mtevL(nldeb_verbose, "[otlpgrpc] grpc incoming payload - client metadata:\n");
  const std::multimap<grpc::string_ref, grpc::string_ref> metadata =
      context->client_metadata();

  std::string check_uuid;
  std::string secret;
  for (auto iter = metadata.begin(); iter != metadata.end(); ++iter) {
    std::string key{iter->first.data(), iter->first.length()};
    mtevL(nldeb_verbose, "[otlpgrpc] header key: %s\n", key.c_str());
    // Check for binary value
    size_t isbin = iter->first.find("-bin");
    if ((isbin != std::string::npos) && (isbin + 4 == iter->first.size())) {
      mtevL(nldeb_verbose, "[otlpgrpc] value: ");
      for (auto c : iter->second) {
        mtevL(nldeb_verbose, "%x", c);
      }
      mtevL(nldeb_verbose, "\n");
      continue;
    }
    std::string value{iter->second.data(), iter->second.length()};
    mtevL(nldeb_verbose, "[otlpgrpc] value: %s\n", value.c_str());
    if (key == "check_uuid") {
      check_uuid = value;
    }
    else if (key == "secret" || key == "api_key") {
      secret = value;
    }
  }

  noit_check_t *check{nullptr};
  uuid_t check_id;
  if (!check_uuid.empty()) {
    mtev_uuid_parse(check_uuid.c_str(), check_id);
    check = noit_poller_lookup(check_id);
    if(!check) {
      return handle_error(std::string{"no such check: "} + check_uuid);
    }
  }
  else {
    return handle_error("no check_uuid specified by grpc metadata");
  }

  if(strcmp(check->module, "otlpgrpc")) {
    return handle_error(std::string("otlpgrpc check not found: " + check_uuid));
  }

  const char *check_secret{nullptr};
  (void)mtev_hash_retr_str(check->config, "secret", strlen("secret"), &check_secret);
  if (secret != check_secret) {
    return handle_error(std::string("incorrect secret specified for check_uuid: ") + check_uuid);
  }

  otlp_upload rxc{check};

  if (const char *mode_str = mtev_hash_dict_get(check->config, "hist_approx_mode")) {
    if(!strcmp(mode_str, "low")) rxc.mode = HIST_APPROX_LOW;
    else if(!strcmp(mode_str, "mid")) rxc.mode = HIST_APPROX_MID;
    else if(!strcmp(mode_str, "harmonic_mean")) rxc.mode = HIST_APPROX_HARMONIC_MEAN;
    else if(!strcmp(mode_str, "high")) rxc.mode = HIST_APPROX_HIGH;
    // Else it just sticks the with initial defaults */
  }

  mtev_memory_init_thread();
  mtev_memory_begin();
  handle_message(&rxc, *request);
  metric_local_batch_flush_immediate(&rxc);
  mtev_memory_end();
  mtev_memory_fini_thread();

  mtevL(nldeb_verbose, "[otlpgrpc] grpc metric data batch submitted successfully.\n");
  return grpc::Status::OK;
}

static int noit_otlpgrpc_onload(mtev_image_t *self) {
  if (!nlerr) nlerr = mtev_log_stream_find("error/otlpgrpc");
  if (!nldeb) nldeb = mtev_log_stream_find("debug/otlpgrpc");
  if (!nldeb_verbose) nldeb_verbose = mtev_log_stream_find("debug/otlpgrpc_verbose");
  if (!nlerr) nlerr = noit_error;
  if (!nldeb) nldeb = noit_debug;
  return 0;
}

static int noit_otlpgrpc_init(noit_module_t *self) {
  const char *config_val;
  otlpgrpc_mod_config *conf = static_cast<otlpgrpc_mod_config*>(noit_module_get_userdata(self));

  conf->grpc_server = "127.0.0.1";
  if (mtev_hash_retr_str(conf->options,
                         "grpc_server", strlen("grpc_server"),
                         (const char **)&config_val)) {
    conf->grpc_server = config_val;
  }

  conf->grpc_port = 4317;
  if (mtev_hash_retr_str(conf->options,
                         "grpc_port", strlen("grpc_port"),
                         (const char **)&config_val)) {
    conf->grpc_port = std::atoi(config_val);
    if (conf->grpc_port <= 0) {
      mtevL(nlerr, "[otlpgrpc] invalid grpc port, using default port 4317\n");
      conf->grpc_port = 4317;
    }
  }

  conf->use_grpc_ssl = false;
  conf->grpc_ssl_use_broker_cert = false;
  conf->grpc_ssl_use_root_cert = false;
  if (mtev_hash_retr_str(conf->options,
                          "use_grpc_ssl", strlen("use_grpc_ssl"),
                          (const char **)&config_val)) {
    if (!strcasecmp(config_val, "true") || !strcasecmp(config_val, "on")) {
      conf->use_grpc_ssl = true;
    }
  }
  if (conf->use_grpc_ssl) {
    if (mtev_hash_retr_str(conf->options,
                            "grpc_ssl_use_broker_cert", strlen("grpc_ssl_use_broker_cert"),
                            (const char **)&config_val)) {
      if (!strcasecmp(config_val, "true") || !strcasecmp(config_val, "on")) {
        conf->grpc_ssl_use_broker_cert = true;
        if (mtev_hash_retr_str(conf->options,
                                "grpc_ssl_use_root_cert", strlen("grpc_ssl_use_root_cert"),
                                (const char **)&config_val)) {
          if (!strcasecmp(config_val, "true") || !strcasecmp(config_val, "on")) {
            conf->grpc_ssl_use_root_cert = true;
          }
        }
      }
    }
  }

  noit_module_set_userdata(self, conf);

  mtevL(nldeb, "[otlpgrpc] server address: %s:%d, use ssl: %s, use broker cert: %s, use root cert: %s\n",
        conf->grpc_server.c_str(), conf->grpc_port, conf->use_grpc_ssl ? "yes" : "no",
        conf->grpc_ssl_use_broker_cert ? "yes" : "no",
        conf->grpc_ssl_use_root_cert ? "yes" : "no"); 

  std::string certificate_file;
  std::string key_file;
  std::string ca_chain;
  if (conf->use_grpc_ssl && conf->grpc_ssl_use_broker_cert) {
    // read cert/key settings from sslconfig
    mtev_conf_section_t listeners = mtev_conf_get_section_read(MTEV_CONF_ROOT,
                                                                "//listeners");
    if (mtev_conf_section_is_empty(listeners)) {
      mtev_conf_release_section_read(listeners);
      mtevL(nlerr, "[otlpgrpc] empty or missing //listeners config.\n");
      return 1;
    }
    const char *value;
    mtev_hash_table *sslconfig = mtev_conf_get_hash(listeners, "sslconfig");
    if (mtev_hash_retr_str(sslconfig, "certificate_file", strlen("certificate_file"), &value)) {
      certificate_file = value;
    }
    if (mtev_hash_retr_str(sslconfig, "key_file", strlen("key_file"), &value)) {
      key_file = value;
    }
    if (certificate_file.empty() || key_file.empty()) {
      mtevL(nlerr, "[otlpgrpc] sslconfig/listeners must have certificate_file and key_file "
                    "configured in order to use grpc ssl with broker cert and key.\n");
      return 1;
    }
    if (conf->grpc_ssl_use_root_cert) {
      if (mtev_hash_retr_str(sslconfig, "ca_chain", strlen("ca_chain"), &value)) {
        ca_chain = value;
      }
      if (ca_chain.empty()) {
        mtevL(nlerr, "[otlpgrpc] sslconfig/listeners must have ca_chain configured for grpc ssl "
                      "using CA root cert\n");
      }
    }

    mtevL(nldeb, "[otlpgrpc] grpc ssl/tls cert settings (if empty, server root will be used):\n");
    mtevL(nldeb, "[otlpgrpc] broker cert is: %s\n", certificate_file.c_str());
    mtevL(nldeb, "[otlpgrpc] broker key is: %s\n", key_file.c_str());
    mtevL(nldeb, "[otlpgrpc] broker ca_chain is: %s\n", ca_chain.c_str());
  }

  std::string server_address = conf->grpc_server + ":" + std::to_string(conf->grpc_port);
  grpcserver = std::make_unique<std::thread>(grpc_server_thread, server_address,
                                conf->use_grpc_ssl, conf->grpc_ssl_use_broker_cert,
                                conf->grpc_ssl_use_root_cert, certificate_file, key_file, ca_chain);
  mtevL(nldeb, "[otlpgrpc] grpc listener thread started\n");

  return 0;
}

#include "otlpgrpc.xmlh"
noit_module_t otlpgrpc = {
  {
    .magic = NOIT_MODULE_MAGIC,
    .version = NOIT_MODULE_ABI_VERSION,
    .name = "otlpgrpc",
    .description = "otlpgrpc collection",
    .xml_description = otlpgrpc_xml_description,
    .onload = noit_otlpgrpc_onload
  },
  noit_otlp_config,
  noit_otlpgrpc_init,
  noit_otlp_initiate_check,
  nullptr
};
