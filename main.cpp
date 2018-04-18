/* Load FPGA from ESP8266 web server
 Maurice Marks, April  2018
 Wemos D1 Mini <-> Upduino FPGA
 Connections:
  SPI interface to  Upduino V1.0 FPGA
  GPIO16 as SS
  GPIO2 as CRESET
  */

#include <Arduino.h>
#include <SPI.h>
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

// Connections
#define FPGA_SS 16
#define FPGA_RESET 2
#define FLASH_SS FPGA_SS

// Web interface
const char* ssid = "<your ssid>";
const char* password = "<your pwd>";

ESP8266WebServer server(80);

const char defpage[] = "<h1>FPGA Upload server<h1>\n \
<form method=\"post\" action=\"/upload\" enctype=\"multipart/form-data\">\n \
<input type=\"file\" name=\"fname\">\n \
<input class=\"button\" type=\"submit\" value=\"Upload\">\n \
</form>\n \
<form action=\"/run\">\n \
  <input type=\"submit\" value=\"Run\" />\n \
</form>\n \
";

#define BUFSIZE 4096
byte buffer[BUFSIZE];
int nbuf, inx;


// Upload file types
typedef enum ftypes {
  TYPE_UNKNOWN,
  TYPE_HEX,
  TYPE_BIN
} FTYPE;

FTYPE ftype;

int flash_written;

int addr; // current flash address
int sector = -1;



// SPI flash routines
byte flash_status();

#define flash_wait  {while (flash_status() & 0x01) {yield();};}

byte gethex(byte c) { // convert char to hex value
  byte val;
  val = 0;
  if ((c >='0') && (c <= '9')) {
    val = c - '0';
  } else if ((c >= 'a') && (c <= 'f')) {
    val = 10 + (c - 'a');
  } else if ((c >= 'A') && (c <= 'F')) {
    val = 10 + (c - 'A');
  } else {
    Serial.print("Illegal hex char "); Serial.println(c);
  }
  return val;
}


void flash_init() {
//    pinMode(FPGA_RESET, OUTPUT);
  // Hold FPGA reset low while we program the Flash !!
  // Otherwise the ICE chip will try to control the flash
    digitalWrite(FPGA_RESET, LOW);
    delay(10);
    pinMode(FLASH_SS, OUTPUT);

    SPI.begin();

    flash_written = 0;
    addr = 0;
    sector = -1; // not erased yet

    // Start up the Flash - wake it up
    digitalWrite(FLASH_SS, LOW);
    SPI.transfer(0xab); //wake
    digitalWrite(FLASH_SS, HIGH);
    yield();
}

byte flash_status() {
  byte s;
  digitalWrite(FLASH_SS, LOW);
  SPI.transfer(0x05); // get Status
  s = SPI.transfer(0);
  digitalWrite(FLASH_SS, HIGH);
  yield();
  return s;
}

void flash_write_enable() {
	digitalWrite(FLASH_SS, LOW);
	SPI.transfer(0x06); // write enable
	digitalWrite(FLASH_SS, HIGH);
}

// Write the current buffer to the flash at the current address (addr)
void flash_write(byte* buf, int nbuf) {
    int i;
    int sec_start, sec_end;
#if 0
// Must deal with page start and size (256 bytes) to get this to work

    // If we are inside an erased 64kb sector just write the entire buffer to Flash
    if (sector >=0) { // we have erased at least one sector
      sec_start = addr >> 16;
      sec_end = (addr+nbuf) >> 16;
      if ((sec_start == sector) && (sec_end == sector)) { // we can write buffer without eraseing
        flash_write_enable();
        digitalWrite(FLASH_SS, LOW);
        SPI.transfer(0x02); // write
        SPI.transfer(addr >> 16);
        SPI.transfer(addr >> 8);
        SPI.transfer(addr);
        for (i=0; i<nbuf; i++) SPI.transfer(buf[i]);
        digitalWrite(FLASH_SS, HIGH);
        flash_wait;
        flash_written += nbuf;
        addr += nbuf;
        return;
      }
    }

#endif
    for (i=0; i<nbuf; i++) {

      if ((addr % (64*1024))==0) {  // we need to erase a sector
        Serial.print("Erasing sector at "); Serial.println(addr, HEX);
        flash_write_enable();
        digitalWrite(FLASH_SS, LOW);
				SPI.transfer(0xd8); //sector Erase
				SPI.transfer(addr >> 16);
				SPI.transfer(addr  >> 8);
				SPI.transfer(addr);
				digitalWrite(FLASH_SS, HIGH);
				flash_wait;
        sector = addr >> 16;
      }

      // Write the current byte - the slow way
       flash_write_enable();
       digitalWrite(FLASH_SS, LOW);
       SPI.transfer(0x02); // write
       SPI.transfer(addr >> 16);
       SPI.transfer(addr >> 8);
       SPI.transfer(addr);
       SPI.transfer(buf[i]);
       digitalWrite(FLASH_SS, HIGH);
       flash_wait;

       flash_written++;
       addr++;
    }

} // flash_write

// maintain continuity between buffers
bool last_seen;
byte last_val;

// Convert a string of hex chars to binary and write them to the flash
void flash_hex_write(byte* ubuf, int ubuflen) {
  int i;
  byte b,c;
  bool seen;

// last buffer ended in the middle of a number
  if (last_seen) {
    seen = last_seen;
    b = last_val;
  } else {
    seen = false;
    b = 0;
  }

  inx = 0;
  nbuf = 0;

  for (i=0; i<ubuflen; i++) {
//    Serial.print(ubuf[i]); Serial.print(" ");
    c = ubuf[i];

    if (c < '0') continue; // ignore all whitespace
    // saw a hex digit
    if (!seen) { // first of pair
      b = gethex(c);
      seen = true;
    } else { // second
      b = (b << 4) + gethex(c);
      // write the byte to our binary buffer
      buffer[inx++] = b;
      nbuf++;
      //reset pair flag
      seen = false;
    }

  }

// maintain continuity between buffers
    if (seen) {
      last_seen = seen;
      last_val = b;
    } else {
      last_seen = false;
      last_val = 0;
    }

// Now write the binary buffer to the Flash at the current address
    flash_write(buffer, nbuf);
}



void flash_done() {
//    while (fpga_written < 200000) fpga_write(0);
    digitalWrite(FPGA_SS, HIGH);
    Serial.println("flash write done");
    Serial.print(flash_written); Serial.println(" bytes written");
    // we are done?
}

void fpga_init() {
    Serial.println("Switching to fpga");

    digitalWrite(FLASH_SS, LOW);
    SPI.transfer(0xb9); // flash sleep
    digitalWrite(FLASH_SS, HIGH);

    SPI.end(); // release SPI pins
    pinMode(FLASH_SS, INPUT); // release the SS pin
    delay(1);
    digitalWrite(FPGA_RESET, HIGH);
    delay(5);
    // Reset the FPGA
    digitalWrite(FPGA_RESET, LOW);
    delay(10);
    digitalWrite(FPGA_RESET, HIGH);
    delay(500); // let it rip
}

void flash_read() {
  int adr = 0;
  int n = 100;
  int i;

  flash_init();
  Serial.println("flash read:");
  inx = 0;
  digitalWrite(FLASH_SS, LOW);
  SPI.transfer(0x03);
  SPI.transfer(adr >> 16);
  SPI.transfer(adr >> 8);
  SPI.transfer(adr);
  while (n--)
    buffer[inx++] = SPI.transfer(0);
  digitalWrite(FLASH_SS, HIGH);

  for (i=0; i<100; i++) {
    Serial.print(buffer[i], HEX); Serial.print(" ");
  }
  Serial.println();
}


/***************************************************/
// Web server response routines

void handle_root() {
  server.send(200, "text/html", defpage);
  delay(100);
}

void handle_done() {
  server.send(200, "text/plain", "Success!");
  delay(100);
  server.sendHeader("Location", "/");
  server.send(303);
}

void handle_read() {
  server.send(200, "text/plain", "Reading flash");
  flash_read();
  delay(100);
}

void handle_run() {
  server.send(200, "text/plain", "Starting fpga");
  fpga_init();
  delay(100);
  // go to upload page
  server.sendHeader("Location", "/");
  server.send(303);
}

void handle_upload() {
  HTTPUpload& upload = server.upload();
  String filename;


  if(upload.status == UPLOAD_FILE_START) {

    filename = upload.filename;
    Serial.print("uploading ");
    Serial.println(filename);
    if (filename.endsWith(".hex")) ftype = TYPE_HEX;
    else if (filename.endsWith(".bin")) ftype = TYPE_BIN;
    else ftype = TYPE_UNKNOWN;

    last_seen = false;
    last_val = 0;

    flash_init();

  } else if (upload.status == UPLOAD_FILE_WRITE) {

    Serial.print("writing chunk of "); Serial.println(upload.currentSize);
    if (ftype==TYPE_HEX) flash_hex_write(upload.buf, upload.currentSize);

  } else if (upload.status == UPLOAD_FILE_END) {

    Serial.print("upload read "); Serial.print(upload.totalSize); Serial.println(" bytes");
    flash_done();
    fpga_init();
    server.sendHeader("Location", "/success");
    server.send(303);
  } else {
    server.send(500, "text/plain", "500: couldnt create file");
  }

} // handle_upload


void handle_id() {
  int id;
  int i;
  // try to read the SPI flash id to check Connections
  // power up the flash
  flash_init();

  // read the id
  digitalWrite(FLASH_SS, LOW);
  SPI.write(0x9f); // get flash id
  //digitalWrite(FLASH_SS, HIGH);

  Serial.println("Flash ID");
  //digitalWrite(FLASH_SS, LOW);
  for(i=0; i<20; i++) {
    Serial.print(SPI.transfer(0), HEX); Serial.print(" ");
  }
  Serial.println();
  digitalWrite(FLASH_SS, HIGH);

  server.sendHeader("Location", "/");
  server.send(303);
//  digitalWrite(FPGA_RESET, HIGH);

}



/********************************************/
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("Starting...");

    pinMode(FPGA_RESET, OUTPUT);
    digitalWrite(FPGA_RESET, LOW);
    pinMode(FLASH_SS, OUTPUT);

    SPI.begin();
    SPI.setFrequency(2000000);

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("");
    Serial.print("Connected! IP address: ");
    Serial.println(WiFi.localIP());

    if (!MDNS.begin("espfpga")) {
      Serial.println("Error in setting up MDNS");
    } else Serial.println("espfpga.local set up");

    // Setup server reponses
    server.on("/",handle_root);

    server.on("/upload", HTTP_POST, [] () {
      server.send(200); },
      handle_upload
    );

    server.on("/flashid", handle_id);

    server.on("/success", handle_done);

    server.on("/read", handle_read);

    server.on("/run", handle_run);

    server.onNotFound([] () {
      server.send(404, "text/plain", "404:  Not Found");
    });


    Serial.println("Ready");

    server.begin();

//    fpga_init();

}

void loop() {

  server.handleClient();

}
