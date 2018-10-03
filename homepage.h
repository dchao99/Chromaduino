#ifndef _INC_PLASMA_HOMEPAGE_H_
#define _INC_PLASMA_HOMEPAGE_H_

const char PROGMEM index_1[] = R"rawliteral(<html><head><script>
var effectEnable = false;
var connection = new WebSocket('ws://'+location.hostname+':81/', ['arduino']);
connection.onopen = function() { connection.send('Connect ' + new Date()); };
connection.onerror = function(error) { console.log('WebSocket Error ', error); };
connection.onmessage = function(e) { console.log('Server: ', e.data); };
function sendInput() {
 var v = parseInt(document.getElementById('v').value).toString(16);
 var s = parseInt(document.getElementById('s').value).toString(16);
 if(v.length<2) { v='0'+v; }  if(s.length<2) { s='0'+s; }
 var input = '#'+s+v; console.log('Input: '+input); connection.send(input); }
function ledEffect () {
 effectEnable = ! effectEnable;
 if (effectEnable) {
  connection.send("Effect ON");
  document.getElementById('effect').style.backgroundColor = '#00878F';
  document.getElementById('v').className = 'disabled';
  document.getElementById('s').className = 'disabled';
  document.getElementById('v').disabled = true;
  document.getElementById('s').disabled = true;
  console.log('LED Effect ON');
 } else {
  connection.send("Normal Mode");
  document.getElementById('effect').style.backgroundColor = '#999';
  document.getElementById('v').className = 'enabled';
  document.getElementById('s').className = 'enabled';
  document.getElementById('v').disabled = false;
  document.getElementById('s').disabled = false;
  console.log('LED Effect OFF');
 }
}</script></head>
<body><center><h2>LED Matrix Control:</h2>
<table><tr>
<td>V: </td><td><input id="v" type="range" min="24" max="255" step="1" value=")rawliteral";

const char PROGMEM index_2[] = R"rawliteral(" oninput="sendInput();" /></td></tr>
<td>S: </td><td><input id="s" type="range" min="40" max="255" step="1" value=")rawliteral";

const char PROGMEM index_3[] = R"rawliteral(" oninput="sendInput();" /></td></tr></table><br/>
<button id="effect" class="button" style="background-color:#999" onclick="ledEffect();">Effect</button><br/><br/>
<font size="1">
Hostname: )rawliteral";

const char PROGMEM index_4[] = R"rawliteral(<br/>
</center></body></html>)rawliteral";


// Construct the home page with current RGBW values
String constructHomePage(uint32_t values)
{
  String buffer;
  char ch[6];
  buffer  = FPSTR(index_1);
  sprintf(ch, "%03d", (values)&0xff);
  buffer += ch;
  buffer += FPSTR(index_2);
  sprintf(ch, "%03d",(values>>8)&0xff );
  buffer += ch;
  buffer += FPSTR(index_3); 
  buffer += WiFi.hostname();
  buffer += FPSTR(index_4); 
  return buffer;
}


// Patch the home page with new input values
// note: buffer will be modified, pass the String object in by reference
void patchHomePage(String& buffer, uint32_t values)
{
  char ch[6];
  int i = strlen_P(index_1);
  sprintf(ch, "%03d", (values)&0xff);
  for (int j=0; j<3; j++)
    buffer.setCharAt(i+j, ch[j]);
  i = i + 3 + strlen_P(index_2);
  sprintf(ch, "%03d",(values>>8)&0xff );
  for (int j=0; j<3; j++)
    buffer.setCharAt(i+j, ch[j]);
}

#endif
