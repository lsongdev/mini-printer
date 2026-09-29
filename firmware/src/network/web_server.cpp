#include "web_server.h"

#include "em_config.h"

#if WIFI_ENABLED

#include "core/print_service.h"
#include "em_device.h"
#include "network/wifi_manager.h"
#include "protocol/print_protocol.h"
#include "transport/transport_common.h"

#include <WiFi.h>
#include <esp_http_server.h>

namespace {
httpd_handle_t server = nullptr;
uint32_t session_counter = 0;

constexpr char INDEX_HTML[] = R"WEB(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Mini Printer</title>
<style>
body{font:16px system-ui,sans-serif;max-width:720px;margin:auto;padding:20px;background:#f6f6f6;color:#111}
section{background:#fff;border:1px solid #ddd;border-radius:12px;padding:16px;margin:0 0 16px}
h1,h2{margin:0 0 12px}label{display:block;margin:10px 0 4px}
button,input,select{font:inherit;padding:9px;box-sizing:border-box}
button{cursor:pointer}select,input[type=password]{width:100%}
.row{display:flex;gap:8px;align-items:center}.row>*{flex:1}
small,#message{color:#555}.ok{color:#176b2c}.bad{color:#a11}
#status{white-space:pre-line}
canvas{width:100%;max-width:384px;background:white;border:1px solid #ddd;image-rendering:pixelated}
code{word-break:break-all}
</style>
</head>
<body>
<h1>Mini Printer</h1>
<section>
<h2>Status</h2>
<div id="status">Loading...</div>
</section>

<section>
<h2>Print image</h2>
<input id="file" type="file" accept="image/*">
<label>Threshold <span id="thresholdValue">160</span></label>
<input id="threshold" type="range" min="0" max="255" value="160">
<label>Density <span id="densityValue">64</span>%</label>
<input id="density" type="range" min="1" max="100" value="64">
<p><canvas id="preview" width="384" height="1"></canvas></p>
<button id="print" disabled>Print</button>
</section>

<section>
<h2>Wi-Fi</h2>
<div class="row">
<select id="networks"><option value="">Scan for networks</option></select>
<button id="scan">Scan</button>
</div>
<label>Password</label>
<input id="password" type="password" autocomplete="current-password">
<p class="row">
<button id="connect">Connect</button>
<button id="forget">Forget saved Wi-Fi</button>
</p>
<small>The setup access point stays available after the printer joins another Wi-Fi network.</small>
</section>

<p id="message"></p>

<script>
const $=id=>document.getElementById(id);
let raster=null;

function setMessage(text,bad=false){
  $("message").textContent=text;
  $("message").className=bad?"bad":"ok";
}

async function refreshStatus(){
  try{
    const s=await fetch("/api/status",{cache:"no-store"}).then(r=>r.json());
    const temp=s.temperature===255?"invalid":s.temperature+" °C";
    const sta=s.wifi.connected
      ? s.wifi.ssid+" · "+s.wifi.ip+" · "+s.wifi.rssi+" dBm"
      : "not connected";
    $("status").textContent=
      "Battery: "+s.battery+"%\n"+
      "Temperature: "+temp+"\n"+
      "Paper: "+(s.paper?"ready":"missing")+"\n"+
      "Printer: "+s.printer+"\n"+
      "Setup AP: "+s.wifi.ap_ssid+"\n"+
      "LAN: "+sta+"\n"+
      "mDNS: http://"+s.wifi.hostname+".local/";
  }catch(e){
    $("status").textContent="Status unavailable";
  }
}

$("threshold").oninput=()=>{
  $("thresholdValue").textContent=$("threshold").value;
  if($("file").files[0]) renderFile($("file").files[0]);
};
$("density").oninput=()=> $("densityValue").textContent=$("density").value;
$("file").onchange=()=> {
  const file=$("file").files[0];
  if(file) renderFile(file);
};

function renderFile(file){
  const url=URL.createObjectURL(file);
  const img=new Image();
  img.onload=()=>{
    const width=384;
    const height=Math.max(1,Math.round(img.height*width/img.width));
    const canvas=$("preview");
    canvas.width=width;
    canvas.height=height;
    const ctx=canvas.getContext("2d",{willReadFrequently:true});
    ctx.fillStyle="#fff";
    ctx.fillRect(0,0,width,height);
    ctx.drawImage(img,0,0,width,height);
    const image=ctx.getImageData(0,0,width,height);
    const t=Number($("threshold").value);
    raster=[];
    for(let y=0;y<height;y++){
      const line=new Uint8Array(48);
      for(let x=0;x<width;x++){
        const i=(y*width+x)*4;
        const gray=image.data[i]*0.299+image.data[i+1]*0.587+image.data[i+2]*0.114;
        const black=gray<t;
        const v=black?0:255;
        image.data[i]=image.data[i+1]=image.data[i+2]=v;
        if(black) line[x>>3]|=0x80>>(x&7);
      }
      raster.push(line);
    }
    ctx.putImageData(image,0,0);
    $("print").disabled=false;
    URL.revokeObjectURL(url);
  };
  img.onerror=()=> {
    URL.revokeObjectURL(url);
    setMessage("Could not decode image",true);
  };
  img.src=url;
}

function frame(type,payload=new Uint8Array()){
  const out=new Uint8Array(6+payload.length);
  out[0]=77; out[1]=80; out[2]=1; out[3]=type;
  out[4]=payload.length&255;
  out[5]=(payload.length>>8)&255;
  out.set(payload,6);
  return out;
}

function printStream(){
  const parts=[frame(1,new Uint8Array([Number($("density").value)]))];
  for(const line of raster) parts.push(frame(2,line));
  parts.push(frame(3));
  const size=parts.reduce((n,p)=>n+p.length,0);
  const body=new Uint8Array(size);
  let offset=0;
  for(const part of parts){ body.set(part,offset); offset+=part.length; }
  return body;
}

$("print").onclick=async()=>{
  if(!raster) return;
  $("print").disabled=true;
  setMessage("Submitting print job...");
  try{
    const r=await fetch("/api/print",{
      method:"POST",
      headers:{"Content-Type":"application/octet-stream"},
      body:printStream()
    });
    const text=await r.text();
    if(!r.ok) throw new Error(text||("HTTP "+r.status));
    setMessage("Print job submitted");
    setTimeout(refreshStatus,300);
  }catch(e){
    setMessage("Print failed: "+e.message,true);
  }finally{
    $("print").disabled=false;
  }
};

$("scan").onclick=async()=>{
  $("scan").disabled=true;
  setMessage("Scanning Wi-Fi...");
  try{
    const items=await fetch("/api/wifi/scan",{cache:"no-store"}).then(async r=>{
      if(!r.ok) throw new Error(await r.text());
      return r.json();
    });
    const select=$("networks");
    select.innerHTML="";
    const seen=new Set();
    for(const item of items){
      if(seen.has(item.ssid)) continue;
      seen.add(item.ssid);
      const o=document.createElement("option");
      o.value=item.ssid;
      o.textContent=item.ssid+" ("+item.rssi+" dBm"+(item.open?", open":"")+")";
      select.appendChild(o);
    }
    setMessage(items.length?"Select a network":"No networks found",!items.length);
  }catch(e){
    setMessage("Wi-Fi scan failed: "+e.message,true);
  }finally{
    $("scan").disabled=false;
  }
};

$("connect").onclick=async()=>{
  const ssid=$("networks").value;
  if(!ssid){ setMessage("Select a Wi-Fi network first",true); return; }
  $("connect").disabled=true;
  setMessage("Connecting to "+ssid+"...");
  try{
    const body=new URLSearchParams({ssid:ssid,password:$("password").value});
    const r=await fetch("/api/wifi/connect",{method:"POST",body:body});
    const text=await r.text();
    if(!r.ok) throw new Error(text||("HTTP "+r.status));
    setMessage("Connected to "+ssid);
    refreshStatus();
  }catch(e){
    setMessage("Wi-Fi connection failed: "+e.message,true);
  }finally{
    $("connect").disabled=false;
  }
};

$("forget").onclick=async()=>{
  const r=await fetch("/api/wifi/forget",{method:"POST"});
  if(r.ok){ setMessage("Saved Wi-Fi forgotten"); refreshStatus(); }
  else setMessage("Could not forget Wi-Fi",true);
};

refreshStatus();
setInterval(refreshStatus,5000);
</script>
</body>
</html>
)WEB";

typedef struct {
    uint32_t session;
    bool job_open;
    bool saw_begin;
    bool saw_end;
} http_print_context_t;

const char *printer_state_name(printer_state_e state)
{
    switch (state)
    {
    case PRINTER_STATUS_IDLE: return "idle";
    case PRINTER_STATUS_PRINTING: return "printing";
    case PRINTER_STATUS_FEEDING: return "feeding";
    case PRINTER_STATUS_ERROR: return "error";
    default: return "unknown";
    }
}

String json_escape(const String &value)
{
    String out;
    out.reserve(value.length() + 8);

    for (size_t i = 0; i < value.length(); ++i)
    {
        const char c = value[i];
        if (c == '"' || c == '\\')
            out += '\\';
        if (static_cast<uint8_t>(c) >= 0x20)
            out += c;
    }

    return out;
}

uint32_t next_session()
{
    ++session_counter;
    if (session_counter == 0 || session_counter >= 0x3FFFFFFFu)
        session_counter = 1;
    return 0x40000000u | session_counter;
}

esp_err_t send_text(
    httpd_req_t *req,
    const char *status,
    const char *type,
    const char *body)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, type);
    return httpd_resp_sendstr(req, body);
}

bool read_body(httpd_req_t *req, char *buffer, size_t size)
{
    if (buffer == nullptr || size == 0 || req->content_len >= size)
        return false;

    size_t offset = 0;
    while (offset < req->content_len)
    {
        const int received = httpd_req_recv(
            req,
            buffer + offset,
            req->content_len - offset);

        if (received <= 0)
            return false;

        offset += static_cast<size_t>(received);
    }

    buffer[offset] = '\0';
    return true;
}

int hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

void url_decode(char *value)
{
    char *src = value;
    char *dst = value;

    while (*src != '\0')
    {
        if (*src == '+')
        {
            *dst++ = ' ';
            ++src;
            continue;
        }

        if (*src == '%' && src[1] != '\0' && src[2] != '\0')
        {
            const int hi = hex_value(src[1]);
            const int lo = hex_value(src[2]);
            if (hi >= 0 && lo >= 0)
            {
                *dst++ = static_cast<char>((hi << 4) | lo);
                src += 3;
                continue;
            }
        }

        *dst++ = *src++;
    }

    *dst = '\0';
}

bool form_value(
    const char *body,
    const char *key,
    char *output,
    size_t output_size)
{
    if (httpd_query_key_value(body, key, output, output_size) != ESP_OK)
        return false;

    url_decode(output);
    return true;
}

esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

esp_err_t status_handler(httpd_req_t *req)
{
    const device_state_t state = get_device_state();
    const wifi_status_t wifi = wifi_manager_status();

    const String ssid = json_escape(String(wifi.ssid));
    const String ip = wifi.connected ? wifi.ip.toString() : "";
    const String json =
        String("{\"battery\":") + state.battery +
        ",\"temperature\":" + state.temperature +
        ",\"paper\":" + (state.paper_state == PAPER_STATUS_NORMAL ? "true" : "false") +
        ",\"printer\":\"" + printer_state_name(state.printer_state) + "\"" +
        ",\"error\":" + static_cast<int>(state.printer_error) +
        ",\"wifi\":{\"connected\":" + (wifi.connected ? "true" : "false") +
        ",\"ssid\":\"" + ssid + "\"" +
        ",\"ip\":\"" + ip + "\"" +
        ",\"rssi\":" + wifi.rssi +
        ",\"ap_ssid\":\"" + json_escape(String(wifi_manager_ap_ssid())) + "\"" +
        ",\"hostname\":\"" + json_escape(String(wifi_manager_hostname())) + "\"}}";

    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return send_text(req, "200 OK", "application/json", json.c_str());
}

esp_err_t wifi_scan_handler(httpd_req_t *req)
{
    const int count = WiFi.scanNetworks(false, true);
    if (count < 0)
        return send_text(
            req,
            "503 Service Unavailable",
            "text/plain",
            "Wi-Fi scan failed");

    httpd_resp_set_status(req, "200 OK");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    if (httpd_resp_send_chunk(req, "[", 1) != ESP_OK)
        return ESP_FAIL;

    for (int i = 0; i < count; ++i)
    {
        const String ssid = json_escape(WiFi.SSID(i));
        const String item =
            String(i == 0 ? "" : ",") +
            "{\"ssid\":\"" + ssid +
            "\",\"rssi\":" + WiFi.RSSI(i) +
            ",\"open\":" + (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") +
            "}";

        if (httpd_resp_send_chunk(req, item.c_str(), item.length()) != ESP_OK)
        {
            WiFi.scanDelete();
            return ESP_FAIL;
        }
    }

    WiFi.scanDelete();
    if (httpd_resp_send_chunk(req, "]", 1) != ESP_OK)
        return ESP_FAIL;
    return httpd_resp_send_chunk(req, nullptr, 0);
}

esp_err_t wifi_connect_handler(httpd_req_t *req)
{
    char body[256];
    char ssid[33];
    char password[65] = {};

    if (!read_body(req, body, sizeof(body)) ||
        !form_value(body, "ssid", ssid, sizeof(ssid)))
    {
        return send_text(
            req,
            "400 Bad Request",
            "text/plain",
            "ssid is required");
    }

    form_value(body, "password", password, sizeof(password));

    if (!wifi_manager_connect(
            ssid,
            password,
            WIFI_CONNECT_TIMEOUT_MS))
    {
        return send_text(
            req,
            "503 Service Unavailable",
            "text/plain",
            "could not connect");
    }

    const wifi_status_t wifi = wifi_manager_status();
    const String json =
        String("{\"connected\":true,\"ssid\":\"") +
        json_escape(String(wifi.ssid)) +
        "\",\"ip\":\"" + wifi.ip.toString() + "\"}";

    return send_text(req, "200 OK", "application/json", json.c_str());
}

esp_err_t wifi_forget_handler(httpd_req_t *req)
{
    const esp_err_t result =
        send_text(req, "200 OK", "application/json", "{\"ok\":true}");
    wifi_manager_forget();
    return result;
}

bool http_print_command(
    const print_protocol_command_t *command,
    void *context_ptr)
{
    http_print_context_t *context =
        static_cast<http_print_context_t *>(context_ptr);

    if (command->type == PRINT_PROTOCOL_STATUS_REQUEST)
        return true;

    const bool accepted = transport_submit_print_command(
        command,
        context->session,
        5000);

    if (!accepted)
        return false;

    if (command->type == PRINT_PROTOCOL_BEGIN)
    {
        context->job_open = true;
        context->saw_begin = true;
    }
    else if (command->type == PRINT_PROTOCOL_END)
    {
        context->job_open = false;
        context->saw_end = true;
    }
    else if (command->type == PRINT_PROTOCOL_CANCEL)
    {
        context->job_open = false;
    }

    return true;
}

esp_err_t print_handler(httpd_req_t *req)
{
    if (req->content_len == 0)
        return send_text(
            req,
            "400 Bad Request",
            "text/plain",
            "empty print stream");

    http_print_context_t context = {
        next_session(),
        false,
        false,
        false,
    };

    print_protocol_parser_t parser;
    print_protocol_init(&parser, http_print_command, &context);

    uint8_t buffer[256];
    size_t remaining = req->content_len;
    bool accepted = true;

    while (remaining > 0 && accepted)
    {
        const size_t wanted =
            remaining < sizeof(buffer)
                ? remaining
                : sizeof(buffer);

        const int received = httpd_req_recv(
            req,
            reinterpret_cast<char *>(buffer),
            wanted);

        if (received <= 0)
        {
            accepted = false;
            break;
        }

        remaining -= static_cast<size_t>(received);
        accepted = print_protocol_feed(
            &parser,
            buffer,
            static_cast<size_t>(received));
    }

    const bool complete_frame =
        parser.header_pos == 0 &&
        parser.payload_pos == 0;

    if (!accepted ||
        remaining != 0 ||
        context.job_open ||
        !context.saw_begin ||
        !context.saw_end ||
        !complete_frame)
    {
        if (context.job_open)
        {
            const print_protocol_error_e error = print_protocol_error(&parser);
            if (error == PRINT_PROTOCOL_ERROR_INVALID_FRAME)
                print_service_abort(context.session, PRINTER_ERROR_PROTOCOL, 1000);
            else
                print_service_cancel(context.session, 1000);
        }

        const print_protocol_error_e error = print_protocol_error(&parser);
        if (error == PRINT_PROTOCOL_ERROR_REJECTED)
        {
            return send_text(
                req,
                "409 Conflict",
                "text/plain",
                "printer busy or command rejected");
        }

        return send_text(
            req,
            "400 Bad Request",
            "text/plain",
            "invalid or incomplete MP stream");
    }

    return send_text(
        req,
        "202 Accepted",
        "application/json",
        "{\"accepted\":true}");
}

esp_err_t captive_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, nullptr, 0);
}

bool register_handler(
    const char *uri,
    httpd_method_t method,
    esp_err_t (*handler_fn)(httpd_req_t *))
{
    httpd_uri_t handler = {};
    handler.uri = uri;
    handler.method = method;
    handler.handler = handler_fn;
    return httpd_register_uri_handler(server, &handler) == ESP_OK;
}
} // namespace

bool init_web_server()
{
    if (server != nullptr)
        return true;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = WIFI_HTTP_PORT;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 8192;

    if (httpd_start(&server, &config) != ESP_OK)
    {
        Serial.println("Failed to start HTTP server");
        server = nullptr;
        return false;
    }

    const bool ok =
        register_handler("/", HTTP_GET, index_handler) &&
        register_handler("/api/status", HTTP_GET, status_handler) &&
        register_handler("/api/wifi/scan", HTTP_GET, wifi_scan_handler) &&
        register_handler("/api/wifi/connect", HTTP_POST, wifi_connect_handler) &&
        register_handler("/api/wifi/forget", HTTP_POST, wifi_forget_handler) &&
        register_handler("/api/print", HTTP_POST, print_handler) &&
        register_handler("/*", HTTP_GET, captive_handler);

    if (!ok)
    {
        Serial.println("Failed to register HTTP handlers");
        httpd_stop(server);
        server = nullptr;
        return false;
    }

    Serial.printf("HTTP server listening on %d\n", WIFI_HTTP_PORT);
    return true;
}

#else

bool init_web_server()
{
    return false;
}

#endif
