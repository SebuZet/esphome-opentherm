#include "hub.h"

namespace esphome {
namespace opentherm {

static const char *TAG = "opentherm";

int16_t get_s16(unsigned long response) {
    return (int16_t) (response & 0xffff);
}

int8_t get_high_s8(unsigned long response) {
    return (int8_t) (response & 0xff);
}

int8_t get_low_s8(unsigned long response) {
    return (int8_t) ((response >> 8) & 0xff);
}

float OpenthermHub::get_t_set_input(byte request_id) {
    if (this->t_set_input_sensor != nullptr) {
        return this->t_set_input_sensor->has_state() ? this->t_set_input_sensor->state : 0.0;
    } else if (this->t_set_input_relative != nullptr) {
        return this->t_set_input_relative->has_state() ? this->t_set_input_relative->state : 0.0;
    } else if (this->t_set_input_number != nullptr) {
        return this->t_set_input_number->has_state() ? this->t_set_input_number->state : 0.0;
    } else {
        return 0.0;
    }
}

void OpenthermHub::publish_to_sensor(OpenthermSensorType type, float state) {
    // auto got = this->sensors.find(type);
    // if (got != this->sensors.end()) {
    //     got->second->publish_state(state);
    // }
}

void OpenthermHub::publish_to_binary_sensor(OpenthermBinarySensorType type, bool state) {
    // auto got = this->binary_sensors.find(type);
    // if (got != this->binary_sensors.end()) {
    //     got->second->publish_state(state);
    // }
}

unsigned int OpenthermHub::build_request(byte request_id) {
    switch (request_id) {
        case OpenThermMessageID::Status:
            ESP_LOGD(TAG, "Building Status request");
            return ot->buildSetBoilerStatusRequest(this->ch_enable, this->dhw_enable, this->cooling_enable, this->otc_active, this->ch2_active);
        case OpenThermMessageID::TSet:
        case OpenThermMessageID::TsetCH2: {
            float target_temp = this->get_t_set_input(request_id);
            ESP_LOGD(TAG, "Building request to set target temperature at %.1f", target_temp);
            unsigned int data = ot->temperatureToData(target_temp);
            return ot->buildRequest(OpenThermMessageType::WRITE_DATA, (OpenThermMessageID)request_id, data);
        }
        case OpenThermMessageID::RelModLevel:
        case OpenThermMessageID::CHPressure:
        case OpenThermMessageID::DHWFlowRate:
        case OpenThermMessageID::Tboiler:
        case OpenThermMessageID::Tdhw:
        case OpenThermMessageID::Toutside:
        case OpenThermMessageID::Tret:
        case OpenThermMessageID::Tstorage:
        case OpenThermMessageID::Tcollector:
        case OpenThermMessageID::TflowCH2:
        case OpenThermMessageID::Tdhw2:
        case OpenThermMessageID::Texhaust:
        case OpenThermMessageID::BurnerStarts:
        case OpenThermMessageID::CHPumpStarts:
        case OpenThermMessageID::DHWPumpValveStarts:
        case OpenThermMessageID::DHWBurnerStarts:
        case OpenThermMessageID::BurnerOperationHours:
        case OpenThermMessageID::CHPumpOperationHours:
        case OpenThermMessageID::DHWPumpValveOperationHours:
        case OpenThermMessageID::DHWBurnerOperationHours:
        case OpenThermMessageID::TdhwSetUBTdhwSetLB:
        case OpenThermMessageID::MaxTSetUBMaxTSetLB:
        case OpenThermMessageID::TdhwSet:
        case OpenThermMessageID::MaxTSet:
        case OpenThermMessageID::SConfigSMemberIDcode:
        case OpenThermMessageID::RBPflags:
            ESP_LOGD(TAG, "Building simple read request with id %d", request_id);
            return ot->buildRequest(OpenThermMessageType::READ_DATA, (OpenThermMessageID)request_id, 0);
    }
    ESP_LOGE(TAG, "Tried to create a request with unknown id %d", request_id);
    return 0;
}

OpenthermHub::OpenthermHub(void(*handle_interrupt_callback)(void), void(*process_response_callback)(unsigned long, OpenThermResponseStatus)) 
    : Component(), handle_interrupt_callback(handle_interrupt_callback), process_response_callback(process_response_callback) {
}

void IRAM_ATTR OpenthermHub::handle_interrupt() {
    this->ot->handleInterrupt();
}

void OpenthermHub::process_response(unsigned long response, OpenThermResponseStatus status) {
    if (!ot->isValidResponse(response)) {
        ESP_LOGW(
            TAG, 
            "Received invalid OpenTherm response: %s, status=%s", 
            String(response, HEX).c_str(),
            String(ot->getLastResponseStatus()).c_str()
        );
        return;
    }

    byte id = (response >> 16 & 0xFF);

    ESP_LOGD(TAG, "Received OpenTherm response with id %d: %s", id, String(response, HEX).c_str());

    switch (id) {
        case OpenThermMessageID::Status:
            this->publish_to_binary_sensor(OpenthermBinarySensorType::fault_indication, ot->isFault(response));
            this->publish_to_binary_sensor(OpenthermBinarySensorType::ch_active, ot->isCentralHeatingActive(response));
            this->publish_to_binary_sensor(OpenthermBinarySensorType::dhw_active, ot->isHotWaterActive(response));
            this->publish_to_binary_sensor(OpenthermBinarySensorType::flame_on, ot->isFlameOn(response));
            this->publish_to_binary_sensor(OpenthermBinarySensorType::cooling_active, ot->isCoolingActive(response));
            this->publish_to_binary_sensor(OpenthermBinarySensorType::ch2_active, response & 0x20);
            this->publish_to_binary_sensor(OpenthermBinarySensorType::diagnostic_indication, ot->isDiagnostic(response));
            break;
        case OpenThermMessageID::TSet:
        case OpenThermMessageID::TsetCH2:
            ESP_LOGD(TAG, "Response temperature: %.1f", ot->getFloat(response));
            break;
        case OpenThermMessageID::RelModLevel:
            this->publish_to_sensor(OpenthermSensorType::rel_mod_level, ot->getFloat(response));
            break;
        case OpenThermMessageID::CHPressure:
            this->publish_to_sensor(OpenthermSensorType::ch_pressure, ot->getFloat(response));
            break;
        case OpenThermMessageID::DHWFlowRate:
            this->publish_to_sensor(OpenthermSensorType::dhw_flow_rate, ot->getFloat(response));
            break;
        case OpenThermMessageID::Tboiler:
            this->publish_to_sensor(OpenthermSensorType::t_boiler, ot->getFloat(response));
            break;
        case OpenThermMessageID::Tdhw:
            this->publish_to_sensor(OpenthermSensorType::t_dhw, ot->getFloat(response));
            break;
        case OpenThermMessageID::Toutside:
            this->publish_to_sensor(OpenthermSensorType::t_outside, ot->getFloat(response));
            break;
        case OpenThermMessageID::Tret:
            this->publish_to_sensor(OpenthermSensorType::t_ret, ot->getFloat(response));
            break;
        case OpenThermMessageID::Tstorage:
            this->publish_to_sensor(OpenthermSensorType::t_storage, ot->getFloat(response));
            break;
        case OpenThermMessageID::Tcollector:
            this->publish_to_sensor(OpenthermSensorType::t_collector, get_s16(response));
            break;
        case OpenThermMessageID::TflowCH2:
            this->publish_to_sensor(OpenthermSensorType::t_flow_ch2, ot->getFloat(response));
            break;
        case OpenThermMessageID::Tdhw2:
            this->publish_to_sensor(OpenthermSensorType::t_dhw2, ot->getFloat(response));
            break;
        case OpenThermMessageID::Texhaust:
            this->publish_to_sensor(OpenthermSensorType::t_exhaust, get_s16(response));
            break;
        case OpenThermMessageID::BurnerStarts:
            this->publish_to_sensor(OpenthermSensorType::burner_starts, ot->getUInt(response));
            break;
        case OpenThermMessageID::CHPumpStarts:
            this->publish_to_sensor(OpenthermSensorType::ch_pump_starts, ot->getUInt(response));
            break;
        case OpenThermMessageID::DHWPumpValveStarts:
            this->publish_to_sensor(OpenthermSensorType::dhw_pump_valve_starts, ot->getUInt(response));
            break;
        case OpenThermMessageID::DHWBurnerStarts:
            this->publish_to_sensor(OpenthermSensorType::dhw_burner_starts, ot->getUInt(response));
            break;
        case OpenThermMessageID::BurnerOperationHours:
            this->publish_to_sensor(OpenthermSensorType::burner_operation_hours, ot->getUInt(response));
            break;
        case OpenThermMessageID::CHPumpOperationHours:
            this->publish_to_sensor(OpenthermSensorType::ch_pump_operation_hours, ot->getUInt(response));
            break;
        case OpenThermMessageID::DHWPumpValveOperationHours:
            this->publish_to_sensor(OpenthermSensorType::dhw_pump_valve_operation_hours, ot->getUInt(response));
            break;
        case OpenThermMessageID::DHWBurnerOperationHours:
            this->publish_to_sensor(OpenthermSensorType::dhw_burner_operation_hours, ot->getUInt(response));
            break;
        case OpenThermMessageID::TdhwSetUBTdhwSetLB:
            this->publish_to_sensor(OpenthermSensorType::t_dhw_set_ub, get_high_s8(response));
            this->publish_to_sensor(OpenthermSensorType::t_dhw_set_lb, get_low_s8(response));
            break;
        case OpenThermMessageID::MaxTSetUBMaxTSetLB:
            this->publish_to_sensor(OpenthermSensorType::max_t_set_ub, get_high_s8(response));
            this->publish_to_sensor(OpenthermSensorType::max_t_set_lb, get_low_s8(response));
            break;
        case OpenThermMessageID::TdhwSet:
            this->publish_to_sensor(OpenthermSensorType::t_dhw_set, ot->getFloat(response));
            break;
        case OpenThermMessageID::MaxTSet:
            this->publish_to_sensor(OpenthermSensorType::max_t_set, ot->getFloat(response));
            // if (this->request_max_t_set) {
            //     this->max_t_set = optional(ot->getFloat(response));
            // }
            break;
        case OpenThermMessageID::SConfigSMemberIDcode:
            this->publish_to_binary_sensor(OpenthermBinarySensorType::dhw_present, response & 0x1);
            this->publish_to_binary_sensor(OpenthermBinarySensorType::control_type_on_off, response & 0x2);
            this->publish_to_binary_sensor(OpenthermBinarySensorType::cooling_supported, response & 0x4);
            this->publish_to_binary_sensor(OpenthermBinarySensorType::dhw_storage_tank, response & 0x8);
            this->publish_to_binary_sensor(OpenthermBinarySensorType::master_pump_control_allowed, response & 0x10);
            this->publish_to_binary_sensor(OpenthermBinarySensorType::ch2_present, response & 0x20);
            break;
        case OpenThermMessageID::RBPflags:
            this->publish_to_binary_sensor(OpenthermBinarySensorType::dhw_setpoint_transfer_enabled, response & 0x1);
            this->publish_to_binary_sensor(OpenthermBinarySensorType::max_ch_setpoint_transfer_enabled, response & 0x2);
            this->publish_to_binary_sensor(OpenthermBinarySensorType::dhw_setpoint_rw, (response >> 8) & 0x1);
            this->publish_to_binary_sensor(OpenthermBinarySensorType::max_ch_setpoint_rw, (response >> 8) & 0x2);
            break;
        default:
            ESP_LOGW(TAG, "This response was not expected.");
            break;
    }
}

void OpenthermHub::setup() {
    ESP_LOGD(TAG, "Setting up OpenTherm component");
    this->ot = new OpenTherm(this->in_pin, this->out_pin, false);
    this->ot->begin(this->handle_interrupt_callback, this->process_response_callback);

    // Ensure that there is at least one request, as we are required to
    // communicate at least once every second
    if (this->repeating_requests.begin() == this->repeating_requests.end()) {
        this->add_repeating_request(OpenThermMessageID::Status);
    }

    this->current_request_iterator = this->initial_requests.begin();
}

void OpenthermHub::on_shutdown() {
    this->ot->end();
}

void OpenthermHub::loop() {
    if (this->ot->isReady()) {
        if (this->initializing && this->current_request_iterator == this->initial_requests.end()) {
            this->initializing = false;
            this->current_request_iterator = this->repeating_requests.begin();
        } else if (this->current_request_iterator == this->repeating_requests.end()) {
            this->current_request_iterator = this->repeating_requests.begin();
        }

        unsigned int request = this->build_request(*this->current_request_iterator);
        this->ot->sendRequestAync(request);
        ESP_LOGD(TAG, "Sent OpenTherm request: %s", String(request, HEX).c_str());
        this->current_request_iterator++;
        
        this->ot->process();
    }
}

#define ID(x) x
#define SHOW2(x) #x
#define SHOW(x) SHOW2(x)

void OpenthermHub::dump_config() {
    ESP_LOGCONFIG(TAG, "OpenTherm:");
    ESP_LOGCONFIG(TAG, "  In: GPIO%d", this->in_pin);
    ESP_LOGCONFIG(TAG, "  Out: GPIO%d", this->out_pin);
    ESP_LOGCONFIG(TAG, "  Sensors: %s", SHOW(OPENTHERM_SENSOR_LIST(ID, )));
    ESP_LOGCONFIG(TAG, "  Binary sensors: %s", SHOW(OPENTHERM_BINARY_SENSOR_LIST(ID, )));
    ESP_LOGCONFIG(TAG, "  Initial requests:");
    for (auto type : this->initial_requests) {
        ESP_LOGCONFIG(TAG, "  - %d", type);
    }
    ESP_LOGCONFIG(TAG, "  Repeating requests:");
    for (auto type : this->repeating_requests) {
        ESP_LOGCONFIG(TAG, "  - %d", type);
    }
}

}  // namespace opentherm
}  // namespace esphome