// SPDX-FileCopyrightText: 2026 CASLab, National Cheng Kung University
//
// SPDX-License-Identifier: Apache-2.0

#include <liblv/binding.h>
#include <liblv/common/tlm_sink.h>
#include <liblv/output.h>
#include <systemc.h>
#include <tlm_core/tlm_2/tlm_generic_payload/tlm_gp.h>

namespace simple {

class ExitCodeRegister : public sc_module {
 public:
  explicit ExitCodeRegister(const sc_module_name &name)
      : sc_module(name), sink_("sink", [this](tlm::tlm_generic_payload &trans) {
          return ProcessRequest(&trans);
        }) {
    SC_THREAD(ProcessRequestThread);
  }

  void ProcessRequestThread() {
    while (true) {
      // blocking call to get a request
      tlm::tlm_generic_payload *trans = sink_.req_port->read();
      ProcessRequest(trans);
      sink_.resp_port->write(trans);
    }
  }

  unsigned int ProcessRequest(tlm::tlm_generic_payload *trans) {
    tlm::tlm_command cmd = trans->get_command();
    sc_dt::uint64 addr = trans->get_address();
    unsigned char *ptr = trans->get_data_ptr();
    unsigned int len = trans->get_data_length();

    if (cmd != tlm::TLM_WRITE_COMMAND) {
      trans->set_response_status(tlm::TLM_COMMAND_ERROR_RESPONSE);
      SC_REPORT_ERROR(
          "TLM-2",
          "Unsupported command for ExitCodeRegister: only write is supported");
      return 0;
    }

    if (addr != 0) {
      trans->set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
      SC_REPORT_ERROR("TLM-2",
                      "Unsupported address for ExitCodeRegister: only address "
                      "0 is supported");
      return 0;
    }

    std::int64_t exit_code = 0;
    switch (len) {
      case 1: {
        std::int8_t v;
        std::memcpy(&v, ptr, sizeof(v));
        exit_code = v;
        break;
      }
      case 2: {
        std::int16_t v;
        std::memcpy(&v, ptr, sizeof(v));
        exit_code = v;
        break;
      }
      case 4: {
        std::int32_t v;
        std::memcpy(&v, ptr, sizeof(v));
        exit_code = v;
        break;
      }
      case 8: {
        std::int64_t v;
        std::memcpy(&v, ptr, sizeof(v));
        exit_code = v;
        break;
      }
      default:
        trans->set_response_status(tlm::TLM_GENERIC_ERROR_RESPONSE);
        SC_REPORT_ERROR("TLM-2",
                        "Unsupported size for ExitCodeRegister: only sizes 1, "
                        "2, 4, and 8 are supported");
        return 0;
    }
    LV_FATAL("{} received exit code: {}", name(), exit_code);

    return len;
  }

  auto port() const { return &sink_.port; }

 private:
  lv::TlmSink sink_;
};

LV_BINDING(simple, ExitCodeRegister)
    .constructor(
        [](const char *name) {
          return std::make_shared<ExitCodeRegister>(name);
        },
        lv::params("name"), lv::doc("Create an exit-code register"))
    .property("port", &ExitCodeRegister::port,
              lv::doc("Exit-code register request port"));

}  // namespace simple
