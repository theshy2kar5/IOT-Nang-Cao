#include <Arduino.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_camera.h>
#include <esp_http_server.h>
#include <esp_system.h>
#include <mbedtls/base64.h>
#include <mbedtls/gcm.h>
#include <time.h>

#include <Person_detection_FOMO_inferencing.h>

#if __has_include("app_secrets.h")
#include "app_secrets.h"
#else
#define SMARTCAM_WIFI_SSID "Redmi K50 Pro"
#define SMARTCAM_WIFI_PASSWORD "hagiabao"
#define SMARTCAM_AES_KEY_HEX ""
#endif

#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

namespace {

constexpr char MQTT_BROKER[] = "broker.hivemq.com";
constexpr uint16_t MQTT_PORT = 8883;
constexpr size_t MQTT_BUFFER_SIZE = 32768;
constexpr uint32_t ALERT_COOLDOWN_MS = 10000;
constexpr uint32_t ALERT_RETRY_INTERVAL_MS = 5000;
constexpr uint32_t WIFI_RECONNECT_INTERVAL_MS = 10000;
constexpr long VIETNAM_UTC_OFFSET_SECONDS = 7 * 60 * 60;
constexpr size_t AES_KEY_SIZE = 32;
constexpr size_t GCM_NONCE_SIZE = 12;
constexpr size_t GCM_TAG_SIZE = 16;

const char HIVE_ROOT_CA[] PROGMEM = R"PEM(
-----BEGIN CERTIFICATE-----
MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF
ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6
b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL
MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv
b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj
ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM
9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw
IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6
VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L
93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm
jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC
AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA
A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI
U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs
N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv
o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU
5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy
rqXRfboQnoZsG4q5WTP468SQvvG5
-----END CERTIFICATE-----
)PEM";

bool debug_nn = false;
uint32_t *snapshot_buf = nullptr;
uint8_t *rgb_frame_buf = nullptr;
WiFiClientSecure mqtt_tls_client;
PubSubClient mqtt_client(mqtt_tls_client);
String device_id;
String alert_topic;
uint8_t aes_key[AES_KEY_SIZE];
bool aes_key_valid = false;
bool camera_ready = false;
bool has_sent_alert = false;
bool has_attempted_alert = false;
bool wifi_was_connected = false;
uint32_t last_alert_ms = 0;
uint32_t last_alert_attempt_ms = 0;
uint32_t last_wifi_reconnect_ms = 0;
httpd_handle_t camera_httpd = nullptr;

esp_err_t cameraPageHandler(httpd_req_t *request) {
    static const char page[] PROGMEM =
        "<!doctype html><html lang=\"vi\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>Camera an ninh</title><style>"
        "body{margin:0;background:#111;color:#eee;font:16px Arial,sans-serif}"
        "main{max-width:900px;margin:0 auto;padding:20px}"
        "h1{font-size:1.4rem}img{display:block;width:100%;height:auto;"
        "background:#222;border-radius:8px}p{color:#bbb}"
        "</style></head><body><main><h1>Camera an ninh - xem trực tiếp</h1>"
        "<img src=\"/stream\" alt=\"Luồng trực tiếp từ camera\">"
        "<p>Nếu hình ảnh bị ngắt, hãy tải lại trang và kiểm tra kết nối Wi-Fi.</p>"
        "</main></body></html>";
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_send(request, page, HTTPD_RESP_USE_STRLEN);
}

esp_err_t streamHandler(httpd_req_t *request) {
    static const char *content_type =
        "multipart/x-mixed-replace;boundary=frame";
    static const char *boundary = "\r\n--frame\r\n";

    esp_err_t result = httpd_resp_set_type(request, content_type);
    if (result != ESP_OK) {
        return result;
    }

    while (true) {
        camera_fb_t *frame = esp_camera_fb_get();
        if (!frame) {
            Serial.println("Không thể lấy khung hình để truyền trực tiếp.");
            return ESP_FAIL;
        }

        char header[64];
        const int header_length = snprintf(
            header, sizeof(header),
            "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
            static_cast<unsigned>(frame->len));
        result = httpd_resp_send_chunk(request, boundary, strlen(boundary));
        if (result == ESP_OK) {
            result = httpd_resp_send_chunk(request, header, header_length);
        }
        if (result == ESP_OK) {
            result = httpd_resp_send_chunk(
                request, reinterpret_cast<const char *>(frame->buf), frame->len);
        }
        esp_camera_fb_return(frame);
        if (result != ESP_OK) {
            return result;
        }
        delay(30);
    }
}

bool startCameraServer() {
    if (camera_httpd) {
        return true;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.ctrl_port = 32768;
    httpd_uri_t page_uri = {};
    page_uri.uri = "/";
    page_uri.method = HTTP_GET;
    page_uri.handler = cameraPageHandler;
    page_uri.user_ctx = nullptr;

    const esp_err_t result = httpd_start(&camera_httpd, &config);
    if (result != ESP_OK) {
        Serial.printf("Khởi động máy chủ hình ảnh thất bại, mã lỗi 0x%x.\n",
                      result);
        camera_httpd = nullptr;
        return false;
    }
    const esp_err_t page_register_result =
        httpd_register_uri_handler(camera_httpd, &page_uri);
    if (page_register_result != ESP_OK) {
        Serial.printf("Đăng ký trang camera thất bại, mã lỗi 0x%x.\n",
                      page_register_result);
        httpd_stop(camera_httpd);
        camera_httpd = nullptr;
        return false;
    }

    httpd_uri_t stream_uri = {};
    stream_uri.uri = "/stream";
    stream_uri.method = HTTP_GET;
    stream_uri.handler = streamHandler;
    stream_uri.user_ctx = nullptr;
    const esp_err_t register_result =
        httpd_register_uri_handler(camera_httpd, &stream_uri);
    if (register_result != ESP_OK) {
        Serial.printf("Đăng ký đường dẫn hình ảnh thất bại, mã lỗi 0x%x.\n",
                      register_result);
        httpd_stop(camera_httpd);
        camera_httpd = nullptr;
        return false;
    }
    Serial.println("Đã bật trang xem camera tại http://<địa-chỉ-IP>/.");
    return true;
}

bool decodeAesKey() {
    const char *hex = SMARTCAM_AES_KEY_HEX;
    if (strlen(hex) != AES_KEY_SIZE * 2) {
        return false;
    }

    for (size_t i = 0; i < AES_KEY_SIZE; ++i) {
        char pair[3] = {hex[i * 2], hex[i * 2 + 1], '\0'};
        char *end = nullptr;
        const unsigned long value = strtoul(pair, &end, 16);
        if (end != pair + 2) {
            return false;
        }
        aes_key[i] = static_cast<uint8_t>(value);
    }
    return true;
}

bool base64Encode(const uint8_t *input, size_t input_length, String &output) {
    const size_t capacity = 4 * ((input_length + 2) / 3) + 1;
    uint8_t *encoded = static_cast<uint8_t *>(malloc(capacity));
    if (!encoded) {
        Serial.println("Không thể cấp phát bộ đệm Base64.");
        return false;
    }

    size_t encoded_length = 0;
    const int result = mbedtls_base64_encode(
        encoded, capacity, &encoded_length, input, input_length);
    if (result != 0) {
        Serial.printf("Mã hóa Base64 thất bại (%d).\n", result);
        free(encoded);
        return false;
    }

    encoded[encoded_length] = '\0';
    output = reinterpret_cast<char *>(encoded);
    free(encoded);
    return true;
}

bool makeTimestamp(String &timestamp) {
    struct tm time_info;
    if (!getLocalTime(&time_info, 100)) {
        return false;
    }

    char buffer[32];
    if (strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S%z", &time_info) == 0) {
        return false;
    }
    timestamp = buffer;
    return true;
}

bool encryptPayload(const String &plaintext, String &iv_base64,
                    String &tag_base64, String &ciphertext_base64) {
    if (!aes_key_valid) {
        Serial.println("Bỏ qua mã hóa cảnh báo: hãy cấu hình SMARTCAM_AES_KEY_HEX trong app_secrets.h.");
        return false;
    }

    uint8_t nonce[GCM_NONCE_SIZE];
    uint8_t tag[GCM_TAG_SIZE];
    esp_fill_random(nonce, sizeof(nonce));

    uint8_t *ciphertext = static_cast<uint8_t *>(malloc(plaintext.length()));
    if (!ciphertext) {
        Serial.println("Không thể cấp phát bộ đệm AES-GCM.");
        return false;
    }

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int result = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, aes_key, 256);
    if (result == 0) {
        result = mbedtls_gcm_crypt_and_tag(
            &gcm, MBEDTLS_GCM_ENCRYPT, plaintext.length(), nonce, sizeof(nonce),
            nullptr, 0,
            reinterpret_cast<const uint8_t *>(plaintext.c_str()),
            ciphertext, sizeof(tag), tag);
    }
    mbedtls_gcm_free(&gcm);

    if (result != 0) {
        Serial.printf("Mã hóa AES-GCM thất bại (%d).\n", result);
        free(ciphertext);
        return false;
    }

    const bool encoded =
        base64Encode(nonce, sizeof(nonce), iv_base64) &&
        base64Encode(tag, sizeof(tag), tag_base64) &&
        base64Encode(ciphertext, plaintext.length(), ciphertext_base64);
    free(ciphertext);
    return encoded;
}

bool connectMqtt() {
    if (mqtt_client.connected()) {
        return true;
    }

    struct tm time_info;
    if (!getLocalTime(&time_info, 100)) {
        Serial.println("Đang chờ đồng bộ giờ mạng để kết nối MQTTS.");
        return false;
    }

    mqtt_client.setServer(MQTT_BROKER, MQTT_PORT);
    mqtt_client.setSocketTimeout(10);
    if (!mqtt_client.connect(device_id.c_str())) {
        Serial.printf("Kết nối MQTTS thất bại, trạng thái=%d.\n", mqtt_client.state());
        return false;
    }
    return true;
}

bool publishAlert(const char *label, float confidence,
                  const uint8_t *jpeg, size_t jpeg_length) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Không gửi được cảnh báo: Wi-Fi đã ngắt kết nối.");
        return false;
    }
    if (!jpeg || jpeg_length == 0) {
        Serial.println("Không gửi được cảnh báo: ảnh JPEG chụp được bị rỗng.");
        return false;
    }

    String image_base64;
    if (!base64Encode(jpeg, jpeg_length, image_base64)) {
        return false;
    }

    JsonDocument alert;
    alert["event"] = "person_detected";
    alert["device_id"] = device_id;
    alert["label"] = label;
    alert["confidence"] = confidence;
    String timestamp;
    if (makeTimestamp(timestamp)) {
        alert["timestamp"] = timestamp;
    } else {
        alert["timestamp"] = nullptr;
        alert["uptime_ms"] = millis();
    }
    alert["battery_percent"] = nullptr;
    alert["battery_status"] = "USB_powered_unknown";
    alert["image_mime"] = "image/jpeg";
    alert["image_base64"] = image_base64;

    String plaintext;
    if (serializeJson(alert, plaintext) == 0 || plaintext.length() == 0) {
        Serial.println("Không thể chuyển cảnh báo thành dữ liệu JSON.");
        return false;
    }

    String iv_base64;
    String tag_base64;
    String ciphertext_base64;
    if (!encryptPayload(plaintext, iv_base64, tag_base64, ciphertext_base64)) {
        return false;
    }

    JsonDocument envelope;
    envelope["version"] = 1;
    envelope["algorithm"] = "AES-256-GCM";
    envelope["iv"] = iv_base64;
    envelope["tag"] = tag_base64;
    envelope["ciphertext"] = ciphertext_base64;
    String mqtt_payload;
    if (serializeJson(envelope, mqtt_payload) == 0 || mqtt_payload.length() == 0) {
        Serial.println("Không thể chuyển cảnh báo đã mã hóa thành dữ liệu MQTT.");
        return false;
    }
    const size_t mqtt_packet_size =
        mqtt_payload.length() + alert_topic.length() + 7;
    if (mqtt_packet_size > MQTT_BUFFER_SIZE) {
        Serial.printf("Gói MQTT quá lớn (%u byte, giới hạn %u byte).\n",
                      static_cast<unsigned>(mqtt_packet_size),
                      static_cast<unsigned>(MQTT_BUFFER_SIZE));
        return false;
    }

    if (!connectMqtt()) {
        return false;
    }

    if (!mqtt_client.publish(alert_topic.c_str(), mqtt_payload.c_str())) {
        Serial.println("Gửi cảnh báo qua MQTT thất bại.");
        return false;
    }
    Serial.printf("Đã gửi cảnh báo mã hóa đến %s (%u byte).\n",
                  alert_topic.c_str(), static_cast<unsigned>(mqtt_payload.length()));
    return true;
}

bool setupCamera() {
    camera_config_t config = {};
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_QQVGA;
    config.jpeg_quality = 18;
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST;

    const esp_err_t result = esp_camera_init(&config);
    if (result != ESP_OK) {
        Serial.printf("Khởi tạo camera thất bại, mã lỗi 0x%x\n", result);
        return false;
    }

    snapshot_buf = static_cast<uint32_t *>(
        malloc(EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT *
               sizeof(uint32_t)));
    if (!snapshot_buf) {
        Serial.println("Không thể cấp phát bộ đệm ảnh suy luận.");
        esp_camera_deinit();
        return false;
    }
    rgb_frame_buf = static_cast<uint8_t *>(ps_malloc(160 * 120 * 3));
    if (!rgb_frame_buf) {
        Serial.println("Không thể cấp phát bộ đệm giải mã JPEG trong PSRAM.");
        free(snapshot_buf);
        snapshot_buf = nullptr;
        esp_camera_deinit();
        return false;
    }
    return true;
}

int rawFeatureGetData(size_t offset, size_t length, float *out_ptr) {
    const size_t pixel_count =
        EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
    if (!snapshot_buf || !out_ptr || offset > pixel_count ||
        length > pixel_count - offset) {
        return -1;
    }
    for (size_t i = 0; i < length; ++i) {
        out_ptr[i] = static_cast<float>(snapshot_buf[offset + i]);
    }
    return 0;
}

} // namespace

bool runInferenceOnce() {
    camera_fb_t *frame = esp_camera_fb_get();
    if (!frame) {
        Serial.println("Chụp ảnh thất bại.");
        return false;
    }

    if (frame->format != PIXFORMAT_JPEG || frame->width == 0 ||
        frame->height == 0 || frame->width > 160 || frame->height > 120) {
        Serial.printf("Camera trả về khung hình không hợp lệ (định dạng=%d, %ux%u, %u byte).\n",
                      frame->format, frame->width, frame->height,
                      static_cast<unsigned>(frame->len));
        esp_camera_fb_return(frame);
        return false;
    }

    if (!fmt2rgb888(frame->buf, frame->len, frame->format, rgb_frame_buf)) {
        Serial.printf("Giải mã ảnh JPEG từ camera thất bại (%u byte).\n",
                      static_cast<unsigned>(frame->len));
        esp_camera_fb_return(frame);
        return false;
    }

    constexpr size_t input_width = EI_CLASSIFIER_INPUT_WIDTH;
    constexpr size_t input_height = EI_CLASSIFIER_INPUT_HEIGHT;
    size_t resized_width;
    size_t resized_height;
    if (static_cast<size_t>(frame->width) * input_height >=
        static_cast<size_t>(frame->height) * input_width) {
        resized_width = input_width;
        resized_height = static_cast<size_t>(frame->height) * input_width /
                         frame->width;
    } else {
        resized_height = input_height;
        resized_width = static_cast<size_t>(frame->width) * input_height /
                        frame->height;
    }
    resized_width = max(static_cast<size_t>(1), resized_width);
    resized_height = max(static_cast<size_t>(1), resized_height);
    const size_t pad_x = (input_width - resized_width) / 2;
    const size_t pad_y = (input_height - resized_height) / 2;
    memset(snapshot_buf, 0, input_width * input_height * sizeof(uint32_t));

    for (size_t y = 0; y < resized_height; ++y) {
        const size_t source_y = y * frame->height / resized_height;
        for (size_t x = 0; x < resized_width; ++x) {
            const size_t source_x = x * frame->width / resized_width;
            const size_t source_offset =
                (source_y * frame->width + source_x) * 3;
            const uint32_t red = rgb_frame_buf[source_offset];
            const uint32_t green = rgb_frame_buf[source_offset + 1];
            const uint32_t blue = rgb_frame_buf[source_offset + 2];
            snapshot_buf[(y + pad_y) * input_width + x + pad_x] =
                (red << 16) | (green << 8) | blue;
        }
    }
    signal_t signal;
    signal.total_length =
        EI_CLASSIFIER_INPUT_WIDTH * EI_CLASSIFIER_INPUT_HEIGHT;
    signal.get_data = rawFeatureGetData;

    ei_impulse_result_t result = {};
    const EI_IMPULSE_ERROR inference_result =
        run_classifier(&signal, &result, debug_nn);
    if (inference_result != EI_IMPULSE_OK) {
        Serial.printf("Bộ phân loại AI chạy thất bại (%d).\n", inference_result);
        esp_camera_fb_return(frame);
        return false;
    }

    const ei_impulse_result_bounding_box_t *best_detection = nullptr;
    for (size_t i = 0; i < result.bounding_boxes_count; ++i) {
        const auto &box = result.bounding_boxes[i];
        if (box.label[0] != '\0' || box.value > 0.0f) {
            Serial.printf("AI dự đoán: %s, độ tin cậy %.3f (ngưỡng %.2f)\n",
                          box.label, box.value,
                          EI_CLASSIFIER_OBJECT_DETECTION_THRESHOLD);
        }
        if (box.value >= EI_CLASSIFIER_OBJECT_DETECTION_THRESHOLD &&
            (!best_detection || box.value > best_detection->value)) {
            best_detection = &box;
        }
    }

    if (best_detection) {
        Serial.printf("Phát hiện đối tượng xâm nhập: %s (độ tin cậy: %.3f)\n",
                      best_detection->label, best_detection->value);
        const uint32_t now = millis();
        if (has_sent_alert &&
            static_cast<uint32_t>(now - last_alert_ms) < ALERT_COOLDOWN_MS) {
            Serial.println("Chưa gửi cảnh báo mới vì vẫn trong thời gian chờ.");
            esp_camera_fb_return(frame);
            return true;
        }
        if (has_attempted_alert &&
            static_cast<uint32_t>(now - last_alert_attempt_ms) <
                ALERT_RETRY_INTERVAL_MS) {
            Serial.println("Đang chờ trước khi thử gửi lại cảnh báo.");
            esp_camera_fb_return(frame);
            return true;
        }

        uint8_t *alert_jpeg = static_cast<uint8_t *>(ps_malloc(frame->len));
        if (!alert_jpeg) {
            Serial.println("Không thể lưu ảnh phát hiện để gửi cảnh báo.");
            esp_camera_fb_return(frame);
            return false;
        }
        const size_t alert_jpeg_length = frame->len;
        memcpy(alert_jpeg, frame->buf, alert_jpeg_length);
        esp_camera_fb_return(frame);
        has_attempted_alert = true;
        last_alert_attempt_ms = millis();
        if (publishAlert(best_detection->label, best_detection->value,
                         alert_jpeg, alert_jpeg_length)) {
            has_sent_alert = true;
            last_alert_ms = millis();
        }
        free(alert_jpeg);
        return true;
    }

    Serial.println("Không phát hiện đối tượng xâm nhập trong ảnh lần này.");
    esp_camera_fb_return(frame);
    return false;
}

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\nCamera an ninh thông minh: chế độ giám sát liên tục");

    aes_key_valid = decodeAesKey();
    if (!aes_key_valid) {
        Serial.println("Chưa cấu hình khóa AES; sẽ không gửi cảnh báo đã mã hóa.");
    }

    if (strlen(SMARTCAM_WIFI_SSID) == 0) {
        Serial.println("Chưa cấu hình thông tin đăng nhập Wi-Fi.");
    } else {
        WiFi.mode(WIFI_STA);
        WiFi.setAutoReconnect(true);
        configTime(VIETNAM_UTC_OFFSET_SECONDS, 0,
                   "pool.ntp.org", "time.cloudflare.com");
        WiFi.begin(SMARTCAM_WIFI_SSID, SMARTCAM_WIFI_PASSWORD);
        Serial.print("Đang kết nối Wi-Fi");
        const uint32_t connect_start = millis();
        while (WiFi.status() != WL_CONNECTED &&
               millis() - connect_start < 20000) {
            delay(500);
            Serial.print(".");
        }
        Serial.println();
    }

    if (WiFi.status() == WL_CONNECTED) {
        wifi_was_connected = true;
        Serial.print("Wi-Fi đã kết nối, địa chỉ IP: ");
        Serial.println(WiFi.localIP());
        struct tm time_info;
        const uint32_t time_start = millis();
        while (!getLocalTime(&time_info, 100) &&
               millis() - time_start < 15000) {
            delay(250);
        }
        if (getLocalTime(&time_info, 100)) {
            Serial.println("Đã đồng bộ thời gian mạng.");
        } else {
            Serial.println("Không lấy được thời gian mạng; có thể không gửi được cảnh báo an toàn.");
        }
    } else {
        Serial.printf(
            "Kết nối Wi-Fi thất bại (trạng thái=%d). Kiểm tra SSID/mật khẩu "
            "và đảm bảo mạng hỗ trợ 2.4 GHz.\n",
            WiFi.status());
    }

    device_id = "smartcam-" + WiFi.macAddress();
    device_id.replace(":", "");
    alert_topic = "smartcam/" + device_id + "/alerts";
    mqtt_tls_client.setCACert(HIVE_ROOT_CA);
    if (!mqtt_client.setBufferSize(MQTT_BUFFER_SIZE)) {
        Serial.println("Không thể cấp phát bộ đệm gói MQTT; gửi cảnh báo sẽ thất bại.");
    }

    camera_ready = setupCamera();
    if (camera_ready) {
        startCameraServer();
    }
}

void loop() {
    if (!camera_ready) {
        delay(3000);
        camera_ready = setupCamera();
        if (camera_ready) {
            startCameraServer();
        }
        return;
    }

    if (strlen(SMARTCAM_WIFI_SSID) > 0) {
        if (WiFi.status() != WL_CONNECTED) {
            wifi_was_connected = false;
            const uint32_t now = millis();
            if (static_cast<uint32_t>(now - last_wifi_reconnect_ms) >=
                WIFI_RECONNECT_INTERVAL_MS) {
                last_wifi_reconnect_ms = now;
                Serial.println("Đang thử kết nối lại Wi-Fi...");
                WiFi.reconnect();
            }
        } else if (!wifi_was_connected) {
            wifi_was_connected = true;
            Serial.print("Wi-Fi đã kết nối, địa chỉ IP: ");
            Serial.println(WiFi.localIP());
        }
    }

    if (mqtt_client.connected()) {
        mqtt_client.loop();
    }

    runInferenceOnce();
    delay(100);
}
