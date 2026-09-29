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
*{box-sizing:border-box}
body{max-width:720px;margin:0 auto;padding:16px;background:#f6f7f9;color:#17212b;font:16px/1.5 system-ui,sans-serif}
.page-header{display:flex;align-items:baseline;justify-content:space-between;gap:16px;margin:4px 0 16px}
h1{font-size:1.7rem;margin:0}h2{font-size:1.1rem;margin:0 0 12px}
.page-nav{display:flex;flex-wrap:wrap;justify-content:flex-end;gap:16px;white-space:nowrap}
.page-nav a{color:#1769c2;text-decoration:none}
.page-nav a:hover,.page-nav a:focus-visible,.page-nav a[aria-current=page]{text-decoration:underline}
section{min-width:0;background:#fff;border:1px solid #dce1e7;border-radius:12px;padding:16px;margin:0 0 16px}
.status-grid{display:grid;grid-template-columns:minmax(0,40%) minmax(0,60%);margin:0}
.status-grid dt,.status-grid dd{padding:9px 0;border-bottom:1px solid #edf0f3;min-width:0}
.status-grid dt{color:#5b6570}.status-grid dd{margin:0;text-align:right;overflow-wrap:anywhere}
.status-grid dt:last-of-type,.status-grid dd:last-of-type{border-bottom:0}
[hidden]{display:none!important}
button,input,select{font:inherit;max-width:100%}
button{min-height:44px;padding:9px 12px;cursor:pointer;border:1px solid #c9d2dc;border-radius:8px;background:#fff}
button:disabled{cursor:default;opacity:.55}
label{display:block;margin:12px 0 5px}
input[type=file],input[type=password],input[type=range],select{display:block;width:100%;min-width:0}
input[type=password],select{min-height:44px;padding:8px;border:1px solid #c9d2dc;border-radius:8px;background:#fff}
.action-button{display:block;width:100%;margin-top:12px;border:0;border-radius:9px;background:#e8edf3;color:#344054}
.action-button.primary{background:#1769c2;color:#fff}
canvas{display:block;max-width:100%;height:auto;background:#fff;border:1px solid #dce1e7;image-rendering:pixelated}
#message{min-height:24px;margin:0 0 12px;overflow-wrap:anywhere}
small{display:block;margin-top:12px;color:#5b6570}
.ok{color:#176b2c}.bad{color:#a11}
</style>
</head>
<body>
<header class="page-header">
<h1>Mini Printer</h1>
<nav class="page-nav" aria-label="Pages">
<a id="print-link" href="#print" aria-current="page">Print</a>
<a id="wifi-link" href="#wifi">Wi-Fi</a>
</nav>
</header>
<section aria-labelledby="status-heading">
<h2 id="status-heading">Status</h2>
<dl id="printerStatus" class="status-grid">Loading...</dl>
</section>
<p id="message" role="status" aria-live="polite"></p>

<div id="print-panel" aria-labelledby="print-link">
<section>
<h2>Print image</h2>
<input id="file" type="file" accept="image/*">
<label>Threshold <span id="thresholdValue">160</span></label>
<input id="threshold" type="range" min="0" max="255" value="160">
<label>Density <span id="densityValue">64</span>%</label>
<input id="density" type="range" min="1" max="100" value="64">
<p><canvas id="preview" width="384" height="1"></canvas></p>
<button id="print" class="action-button primary" disabled>Print</button>
</section>
</div>

<div id="wifi-panel" aria-labelledby="wifi-link" hidden>
<section>
<h2>Connect to Wi-Fi</h2>
<label for="networks">Network</label>
<select id="networks"><option value="">Scan for networks</option></select>
<button id="scan" class="action-button">Scan</button>
<label for="password">Password</label>
<input id="password" type="password" autocomplete="current-password">
<button id="connect" class="action-button primary">Connect &amp; save</button>
<button id="forget" class="action-button">Forget saved Wi-Fi</button>
<small>Credentials are saved after a successful connection and restored on restart. The setup AP stays available.</small>
</section>
</div>

<script>
const $=id=>document.getElementById(id);
let raster=null;

function setMessage(text,bad=false){
  $("message").textContent=text;
  $("message").className=bad?"bad":"ok";
}

function selectTab(name){
  for(const tabName of ["print","wifi"]){
    const active=tabName===name;
    if(active) $(tabName+"-link").setAttribute("aria-current","page");
    else $(tabName+"-link").removeAttribute("aria-current");
    $(tabName+"-panel").hidden=!active;
  }
}

window.addEventListener("hashchange",()=>selectTab(location.hash==="#wifi"?"wifi":"print"));
selectTab(location.hash==="#wifi"?"wifi":"print");

function setStatusRows(id,rows){
  const list=$(id);
  list.replaceChildren();
  for(const [label,value] of rows){
    const name=document.createElement("dt");
    const detail=document.createElement("dd");
    name.textContent=label;
    detail.textContent=value;
    list.append(name,detail);
  }
}

async function refreshStatus(){
  try{
    const s=await fetch("/api/status",{cache:"no-store"}).then(r=>r.json());
    setStatusRows("printerStatus",[
      ["Battery",s.battery+"%"],
      ["Print head",s.temperature===255?"Unavailable":s.temperature+" °C"],
      ["Paper",s.paper?"Ready":"Missing"],
      ["Printer",s.printer],
      ["Saved network",s.wifi.saved_ssid||"None"],
      ["Connection",s.wifi.connected?s.wifi.ssid:"Not connected"],
      ["IP address",s.wifi.connected?s.wifi.ip:"—"],
      ["Signal",s.wifi.connected?s.wifi.rssi+" dBm":"—"],
      ["Setup AP",s.wifi.ap_ssid],
      ["Device URL","http://"+s.wifi.hostname+".local/"]
    ]);
  }catch(e){
    $("printerStatus").textContent="Status unavailable";
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
  setMessage("Connecting to "+ssid+" and saving credentials...");
  try{
    const body=new URLSearchParams({ssid:ssid,password:$("password").value});
    const r=await fetch("/api/wifi/connect",{method:"POST",body:body});
    if(!r.ok) throw new Error(await r.text()||("HTTP "+r.status));
    const result=await r.json();
    setMessage("Connected to "+result.ssid+" and saved. IP: "+result.ip);
    await refreshStatus();
  }catch(e){
    setMessage("Wi-Fi connection failed: "+e.message,true);
  }finally{
    $("connect").disabled=false;
  }
};

$("forget").onclick=async()=>{
  try{
    const r=await fetch("/api/wifi/forget",{method:"POST"});
    if(!r.ok) throw new Error(await r.text()||("HTTP "+r.status));
    setMessage("Saved Wi-Fi forgotten");
    await refreshStatus();
  }catch(e){
    setMessage("Could not forget Wi-Fi: "+e.message,true);
  }
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
    Serial.println("HTTP GET /");
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    const esp_err_t result = httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
    if (result != ESP_OK)
        Serial.printf("HTTP GET / failed: %s\n", esp_err_to_name(result));
    return result;
}

esp_err_t status_handler(httpd_req_t *req)
{
    const device_state_t state = get_device_state();
    const wifi_status_t wifi = wifi_manager_status();

    const String ssid = json_escape(String(wifi.ssid));
    const String ip = wifi.connected ? wifi.ip.toString() : "";
    const String json =
        String("{\"battery\":") + static_cast<int>(state.battery) +
        ",\"temperature\":" + static_cast<int>(state.temperature) +
        ",\"paper\":" + (state.paper_state == PAPER_STATUS_NORMAL ? "true" : "false") +
        ",\"printer\":\"" + printer_state_name(state.printer_state) + "\"" +
        ",\"error\":" + static_cast<int>(state.printer_error) +
        ",\"wifi\":{\"connected\":" + (wifi.connected ? "true" : "false") +
        ",\"ssid\":\"" + ssid + "\"" +
        ",\"ip\":\"" + ip + "\"" +
        ",\"rssi\":" + wifi.rssi +
        ",\"saved_ssid\":\"" + json_escape(String(wifi.saved_ssid)) + "\"" +
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
    // Values are URL encoded, so reserve up to three bytes of form text for
    // every decoded byte (%XX). ESP32 Wi-Fi limits SSIDs to 32 bytes.
    char body[384];
    char ssid[97];
    char password[193] = {};

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

    if (strlen(ssid) > 32 || strlen(password) > 64)
    {
        return send_text(
            req,
            "400 Bad Request",
            "text/plain",
            "ssid or password is too long");
    }

    const wifi_connect_result_e result = wifi_manager_connect(
        ssid,
        password,
        WIFI_CONNECT_TIMEOUT_MS);
    if (result == wifi_connect_result_e::connection_failed)
    {
        return send_text(
            req,
            "503 Service Unavailable",
            "text/plain",
            "could not connect");
    }
    if (result == wifi_connect_result_e::save_failed)
    {
        return send_text(
            req,
            "500 Internal Server Error",
            "text/plain",
            "connected, but could not save Wi-Fi credentials");
    }

    const wifi_status_t wifi = wifi_manager_status();
    const String json =
        String("{\"connected\":true,\"saved\":true,\"ssid\":\"") +
        json_escape(String(wifi.ssid)) +
        "\",\"ip\":\"" + wifi.ip.toString() + "\"}";

    return send_text(req, "200 OK", "application/json", json.c_str());
}

esp_err_t wifi_forget_handler(httpd_req_t *req)
{
    if (!wifi_manager_forget())
        return send_text(req, "500 Internal Server Error", "text/plain", "could not forget saved Wi-Fi");
    return send_text(req, "200 OK", "application/json", "{\"ok\":true}");
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
    config.lru_purge_enable = true;

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
