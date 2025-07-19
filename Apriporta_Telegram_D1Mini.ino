/*
 * ####################################################
 * ESP8266 D1 mini - Telegram Bot Citofono            #
 * Controllo porta e luce scale tramite bot Telegram  #
 * Visualizzazione stato e log su display OLED        #
 * Configurazione WiFi e BOT_TOKEN via Web Portal     #
 * ####################################################
 *
 * Questo programma trasforma un ESP8266 D1 mini in un sistema di controllo remoto
 * per un citofono/apriporta tramite Telegram. Include un'interfaccia Web per la configurazione,
 * un display OLED per mostrare lo stato e un sistema di log per tracciare le operazioni.
 *
 * Il sistema permette:
 * - Apertura della porta tramite pulsante fisico o comando Telegram
 * - Accensione della luce scale tramite comando Telegram
 * - Visualizzazione in tempo reale sul display OLED
 * - Configurazione WiFi e token bot tramite pagina Web
 * - Logging delle operazioni (ultime 5)
 * - Controllo degli accessi per chat ID autorizzati
 *
 * ISTRUZIONI SETUP:
 * 1. Installa le librerie necessarie dal Library Manager:
 *    - UniversalTelegramBot by Brian Lough (compatibile con ESP8266)
 *    - ArduinoJson by Benoit Blanchon (versione 6.x)
 *    - Adafruit_GFX
 *    - Adafruit_SSD1306
 *    - ESP8266WiFi, ESP8266WebServer, DNSServer
 * 
 * 2. Alla prima accensione o in caso di errore WiFi,
 *    il dispositivo crea una rete WiFi "ESP_Config".
 *    Collegati con PC o smartphone e vai su 192.168.4.1
 *    per inserire SSID, password e BOT_TOKEN.
 * 
 * 3. Dopo la configurazione, il sistema si riavvia e si connette
 *    automaticamente ai servizi configurati.
 *
 * 4. Utilizza un display Oled da 0.92 inch SSD1306
 *    di 128x64 pixel.
 * 
 * COLLEGAMENTI HARDWARE (per D1 mini):
 * - D5 (GPIO14)  -> Relè Porta 
 * - D8 (GPIO15)  -> Relè Luce 
 * - D4 (GPIO2)   -> LED Status (LED blu integrato sul D1 mini)
 * - D7 (GPIO13)  -> Buzzer (opzionale)
 * - D1 (GPIO5)   -> SCL OLED Display
 * - D2 (GPIO4)   -> SDA OLED Display
 * - D3 (GPIO0)   -> Pulsante (NO=Normalmente Aperto) tra GPIO0 e massa
 * - D0 (GPIO16)  -> Pulsante (NO=Normalmente Aperto) tra GPIO16 e massa
 * - 3.3V/5V -> VCC dei relè
 * - 3.3V Vcc OLED Display
 * - GND     -> GND comune/GND OLED Display
 *
 *
 */
 


// ====== INCLUDE E DEFINIZIONI ======
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>

// =================== CONFIGURAZIONE DINAMICA ===================
// Definizione delle dimensioni massime per le stringhe in EEPROM
#define MAX_SSID_LEN 32
#define MAX_PASS_LEN 64
#define MAX_TOKEN_LEN 64
#define MAX_PASSCFG_LEN 32
#define MAX_USERS_LEN 128   // Lista chat ID autorizzati
#define MAX_USERS 10        // Max numero di utenti

// Struttura per memorizzare la configurazione in EEPROM
struct Config {
  char ssid[MAX_SSID_LEN];           // SSID della rete WiFi
  char password[MAX_PASS_LEN];       // Password WiFi
  char botToken[MAX_TOKEN_LEN];      // Token del bot Telegram
  char configPassword[MAX_PASSCFG_LEN]; // Password per accedere alla pagina web
  char authorizedUsers[MAX_USERS_LEN]; // Chat ID autorizzati, separati da virgola
  bool configured;                   // Indica se la configurazione è stata completata
};

Config userConfig; // Variabile globale per la configurazione

#define EEPROM_SIZE sizeof(Config) // Dimensione della EEPROM necessaria

// ====== ARRAY PER GLI UTENTI AUTORIZZATI ======
long authorizedUsers[MAX_USERS]; // Array di chat ID autorizzati
int numAuthorizedUsers = 0;      // Contatore degli utenti autorizzati


// =================== PIN CONFIGURATION ===================
#define RELAY_PORTA D5       // GPIO14 <-- Relè High per attivare Porta
#define RELAY_LUCE  D8       // GPIO15 <-- Relè High per attivare Luce
#define LED_STATUS  D4       // GPIO2  <-- LED BuiltIn
#define BUZZER_PIN  D7       // GPIO13 <-- Buzzer
#define BUTTON_PORTA D3      // GPIO0  <-- Pulsante fisico apriporta
#define RESET_BUTTON_PIN D0  // GPIO16 <-- Pulsante per entrare in Config

// ================== TIMING CONFIGURATION =================
const unsigned long PORTA_TIMEOUT = 1000;   // Porta aperta per 1 secondo
const unsigned long LUCE_TIMEOUT = 1000;    // Luce accesa per 1 secondo
const int BOT_REQUEST_DELAY = 500;          // Controlla messaggi ogni mezzo secondo

// ===================== DISPLAY OLED ======================
#define SCREEN_WIDTH 128 // Larghezza display OLED (in pixel)
#define SCREEN_HEIGHT 64 // Altezza display OLED
#define OLED_RESET    -1 // Reset non necessario per SSD1306
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET); // Oggetto display OLED

// ==================== VARIABILI GLOBALI ==================
WiFiClientSecure client; // Client per connessione sicura
UniversalTelegramBot bot("", client); // Bot Telegram (token verrà impostato dopo)
unsigned long lastTimeBotRan; // Timestamp dell'ultimo polling Telegram
unsigned long portaOpenTime = 0; // Timestamp dell'ultima apertura della porta
unsigned long luceOnTime = 0;    // Timestamp dell'ultima accensione della luce

// ============= VARIABILI GLOBALI INTERRUPT ===============
volatile bool buttonInterruptFlag = false; // Flag per l'interrupt del pulsante
volatile unsigned long lastInterruptTime = 0; // Timestamp dell'ultimo interrupt
const unsigned long interruptDebounceDelay = 100; // Tempo di debounce per il pulsante

// ====== PER MODALITÀ CONFIGURAZIONE DA PULSANTE ======
unsigned long resetButtonPressTime = 0; // Tempo in cui è stato premuto il pulsante di reset
bool configModeRequested = false;       // Flag per entrare in modalità configurazione

// ============== STATO CONNESSIONE DISPLAY ================
bool wifiOk = false;  // Flag per stato WiFi
bool telegramOk = false; // Flag per stato Telegram
String lastDisplayContent = ""; // Ultimo contenuto mostrato sul display

// ================= STRUTTURA PER LOGGING =================
struct LogEntry {
  unsigned long timestamp; // Quando è avvenuta l'operazione
  long chatId;             // Chat ID dell'utente che ha eseguito l'operazione
  String operation;        // Tipo di operazione (es. "PORTA_APERTA")
  String userName;         // Nome dell'utente che ha eseguito l'operazione
};
LogEntry operationLog[5]; // Registro delle ultime 5 operazioni

// ============= FUNZIONE ISR PER IL PULSANTE ==============
/**
 * @brief Gestore dell'interrupt del pulsante fisico.
 *        Attiva il flag solo dopo un debounce per evitare falsi trigger.
 */
void ICACHE_RAM_ATTR buttonISR() {
  unsigned long now = millis();
  // Debounce: accetta solo se è passato abbastanza tempo dall'ultimo trigger
  if (now - lastInterruptTime > interruptDebounceDelay) {
    buttonInterruptFlag = true;
    lastInterruptTime = now;
  }
}

// ============= FUNZIONI DISPLAY CENTRATO ================
/**
 * @brief Stampa testo centrato sul display OLED.
 *        Utile per titoli o messaggi principali.
 * 
 * @param text Testo da stampare
 * @param y Posizione Y del testo
 * @param textSize Dimensione del testo (1x, 2x...)
 */
void drawCenteredText(String text, int y, int textSize = 1) {
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextSize(textSize);
  display.getTextBounds(text.c_str(), 0, y, &x1, &y1, &w, &h);
  int x = (SCREEN_WIDTH - w) / 2; // Calcola posizione centrata
  display.setCursor(x, y);
  display.print(text);
  display.setTextSize(1); // Ripristina la dimensione standard
}

// =================== FUNZIONI DISPLAY ====================
/**
 * @brief Formatta il tempo trascorso in formato leggibile abbreviato (es. "3m", "5h").
 *        Usato nei log per mostrare l'ultima operazione.
 */
String formatTimeAgoShort(unsigned long ago) {
  unsigned long seconds = ago / 1000;
  if (seconds < 60) return String(seconds) + "s ";
  unsigned long minutes = seconds / 60;
  if (minutes < 60) return String(minutes) + "m ";
  unsigned long hours = minutes / 60;
  if (hours < 24) return String(hours) + "h ";
  unsigned long days = hours / 24;
  return String(days) + "g ";
}

/**
 * @brief Restituisce una riga formattata per il log sul display OLED.
 *        Mostra nome utente, tipo di operazione e quando è stata eseguita.
 */
String getShortLogLine(int idx) {
  if (operationLog[idx].timestamp == 0) return "";
  String action;
  if (operationLog[idx].operation == "LUCE_ACCESA") action = "Luce";
  else if (operationLog[idx].operation == "PORTA_PULSANTE") action = "Porta";
  else action = "Porta";
  String name = operationLog[idx].userName;
  String ago = formatTimeAgoShort(millis() - operationLog[idx].timestamp);
  return name + " " + action + " " + ago;
}

/**
 * @brief Restituisce lo stato della connessione WiFi e Telegram per il display.
 */
String getConnStatusLine() {
  String w = wifiOk ? "WiFi:OK" : "WiFi:--";
  String t = telegramOk ? "TG:OK" : "TG:--";
  return w + " " + t;
}

/**
 * @brief Aggiorna il display OLED con informazioni correnti:
 *        - Titolo
 *        - Ultime operazioni
 *        - Stato connessioni
 */
void updateDisplay() {
  String line1 = "D1 Mini Apriporta";
  String line2 = getShortLogLine(0);
  String line3 = getShortLogLine(1);
  String line4 = getShortLogLine(2);   
  String line5 = getShortLogLine(3);
  String line6 = getConnStatusLine();

  String displayContent = line1 + "|" + line2 + "|" + line3 + "|" + line4 + "|" + line5 + "|" + line6;
  
  // Evita aggiornamenti inutili
  if (displayContent == lastDisplayContent) return;
  lastDisplayContent = displayContent;

  // Pulisce e reimposta il display
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Stampa le righe
  drawCenteredText("D1 Mini Apriporta", 0, 1);
  display.setCursor(0, 16);
  display.println(line2);
  display.setCursor(0, 26); 
  display.println(line3);
  display.setCursor(0, 36); 
  display.println(line4);
  display.setCursor(0, 46); 
  display.println(line5);
  drawCenteredText(getConnStatusLine(), 57);
  display.display();
}

// ==================== EEPROM CONFIG ======================
/**
 * @brief Carica la configurazione salvata in EEPROM.
 *        Se non è mai stata configurata, usa valori predefiniti.
 */
void loadConfig() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, userConfig);

  // Forza la terminazione delle stringhe
  userConfig.ssid[MAX_SSID_LEN-1] = '\0';
  userConfig.password[MAX_PASS_LEN-1] = '\0';
  userConfig.botToken[MAX_TOKEN_LEN-1] = '\0';
  userConfig.configPassword[MAX_PASSCFG_LEN-1] = '\0';
  userConfig.authorizedUsers[MAX_USERS_LEN-1] = '\0';

  // Se non configurato, azzera tutto
  if (userConfig.configured != true) {
    memset(&userConfig, 0, sizeof(userConfig));
    userConfig.configured = false;
  }
  // Inizializza password default se vuota
  if (userConfig.configPassword[0] == '\0') {
    strncpy(userConfig.configPassword, "admin", MAX_PASSCFG_LEN-1);
  }
  EEPROM.end();
}

// ============ SALVA CONFIGURAZIONE IN EEPROM =============
/**
 * @brief Salva la configurazione corrente in EEPROM.
 */
void saveConfig() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.put(0, userConfig);
  EEPROM.commit(); // Scrive effettivamente i dati
  EEPROM.end();
}

// ===== PARSING DELLA STRINGA UTENTI IN ARRAY DI LONG =====
/**
 * @brief Converte la stringa degli utenti autorizzati in array di chat ID.
 *        Ogni chat ID è separato da virgola.
 */
void parseAuthorizedUsers() {
  numAuthorizedUsers = 0;
  char usersCopy[MAX_USERS_LEN];
  strncpy(usersCopy, userConfig.authorizedUsers, MAX_USERS_LEN-1);
  usersCopy[MAX_USERS_LEN-1] = '\0'; // Assicura terminazione stringa

  char* token = strtok(usersCopy, ",");
  while (token != NULL && numAuthorizedUsers < MAX_USERS) {
    authorizedUsers[numAuthorizedUsers] = atol(token);
    Serial.print("Utente autorizzato caricato: ");
    Serial.println(authorizedUsers[numAuthorizedUsers]);
    numAuthorizedUsers++;
    token = strtok(NULL, ",");
  }
  Serial.print("Totale utenti autorizzati: ");
  Serial.println(numAuthorizedUsers);
}

// =============== WEB PORTAL CONFIGURAZIONE ===============
/**
 * @brief Porta su cui il server DNS risponde.
 *        Solitamente la porta 53 per il traffico DNS standard.
 */
const byte DNS_PORT = 53;

/**
 * @brief Oggetto DNSServer per gestire le richieste DNS durante la modalità configurazione.
 *        Reindirizza tutte le richieste verso l'IP dell'ESP8266.
 */
DNSServer dnsServer;

/**
 * @brief Server web che gestisce la pagina di configurazione.
 *        Ascolta sulla porta 80.
 */
ESP8266WebServer server(80);

/**
 * @brief Flag che indica se l'utente ha inserito correttamente la password di configurazione.
 */
bool passwordOk = false;

// ======= FUNZIONE RESET EEPROM =======
/**
 * @brief Resetta completamente la EEPROM azzerando tutti i dati salvati.
 *        Utile in caso di errore o per ripristinare le impostazioni predefinite.
 */
 void resetEEPROM() {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0); // scrive zero in ogni cella
  }
  EEPROM.commit(); // salva i dati sulla flash
  EEPROM.end();
}

/**
 * @brief Gestore della richiesta HTTP per resettare la EEPROM.
 *        Chiama `resetEEPROM()` e riavvia l'ESP8266.
 */
void handleResetEEPROM() {
  resetEEPROM();
  server.send(200, "text/html", "<h1>EEPROM Resettata!</h1><p>Riavvio in corso...</p>");
  delay(2000);
  ESP.restart(); // Riavvia il dispositivo dopo il reset
}

// ====== PAGINA PRINCIPALE / LOGIN / CAMBIO PASSWORD ======
/**
 * @brief Gestore principale delle richieste HTTP alla root `/`.
 *        Mostra la pagina di login se non autenticato,
 *        oppure la pagina di configurazione se autenticato.
 */
void handleRoot() {
  if (!passwordOk) {
    // Pagina di login
    String html = R"rawliteral(
      <!DOCTYPE HTML>
      <html>
      <head>
        <title>Login Configurazione</title>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <style>
          button, input[type=submit] {
            background-color: #2196F3;
            color: white;
            font-size: 12px;
            padding: 10px 20px;
            border: none;
            border-radius: 5px;
            margin-top: 10px;
            cursor: pointer;
          }
          button:active, input[type=submit]:active {
            background-color: #1769aa;
          }
          .red {
            background-color: #e53935 !important;
          }
        </style>
      </head>
      <body>
        <h1>Inserisci password configurazione</h1>
        <form action="/login" method="post">
          <label for="cfgpass">Password:</label><br>
          <input type="password" id="cfgpass" name="cfgpass" required><br><br>
          <input type="submit" value="Accedi">
        </form>
        <br><br><br>
        <form action="/reset_eeprom" method="post" onsubmit="return confirm('Sei sicuro di voler cancellare tutta la memoria e riportare il sistema ai valori di fabbrica?');">
          <button type="submit" class="red">Reset EEPROM (Factory Reset)</button>
        </form>
      </body>
      </html>
    )rawliteral";
    server.send(200, "text/html", html);
    return;
  }

  // Pagina di configurazione principale
  String html = R"rawliteral(
    <!DOCTYPE HTML>
    <html>
    <head>
      <title>Configurazione ESP</title>
      <meta name="viewport" content="width=device-width, initial-scale=1">
      <style>
        button, input[type=submit] {
          background-color: #2196F3;
          color: white;
          font-size: 12px;
          padding: 10px 20px;
          border: none;
          border-radius: 5px;
          margin-top: 10px;
          cursor: pointer;
        }
        button:active, input[type=submit]:active {
          background-color: #1769aa;
        }
        a.button-link {
          display: inline-block;
          background-color: #2196F3;
          color: white !important;
          font-size: 12px;
          padding: 10px 20px;
          border: none;
          border-radius: 5px;
          margin-top: 10px;
          text-decoration: none;
          cursor: pointer;
        }
        a.button-link:active {
          background-color: #1769aa;
        }
      </style>
    </head>
    <body>
      <h1>Configurazione D1 Mini Apriporta</h1>
      <form action="/save" method="post">
        <label for="ssid">SSID Rete WiFi:</label><br>
        <input type="text" id="ssid" name="ssid" value="%SSID%" required><br><br>
        <label for="pass">Password Rete WiFi:</label><br>
        <input type="password" id="pass" name="pass" value="%PASS%" required><br><br>
        <label for="token">BOT_TOKEN Telegram:</label><br>
        <input type="text" id="token" name="token" value="%BOT_TOKEN%" required><br><br>
        <label for="users">Utenti autorizzati (chat ID separati da virgola):</label><br>
        <input type="text" id="users" name="users" value="%USERS%" required><br><br>
        <input type="submit" value="Salva e Riavvia">
      </form>
      <br>
      <a href="/changepw" class="button-link">Cambia Password Configurazione</a>
    </body>
    </html>
  )rawliteral";

  html.replace("%SSID%", String(userConfig.ssid));
  html.replace("%PASS%", String(userConfig.password));
  html.replace("%BOT_TOKEN%", String(userConfig.botToken));
  html.replace("%USERS%", String(userConfig.authorizedUsers));

  server.send(200, "text/html", html);
}

/**
 * @brief Gestore della richiesta POST per il login alla pagina di configurazione.
 *        Verifica la password e permette l'accesso se corretta.
 */
void handleLogin() {
  if (server.hasArg("cfgpass") && strcmp(server.arg("cfgpass").c_str(), userConfig.configPassword) == 0) {
    passwordOk = true;
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  } else {
    server.send(403, "text/html", "<h1>Password configurazione errata!</h1>");
  }
}

/**
 * @brief Gestore della richiesta POST per salvare i nuovi parametri di configurazione.
 *        Aggiorna SSID, password WiFi, token bot e utenti autorizzati.
 */
void handleSave() {
  if (!passwordOk) {
    server.send(403, "text/html", "<h1>Non autorizzato</h1>");
    return;
  }
  if (server.hasArg("ssid") && server.hasArg("pass") && server.hasArg("token") && server.hasArg("users")) {
    String newSsid = server.arg("ssid");
    String newPass = server.arg("pass");
    String newToken = server.arg("token");
    String newUsers = server.arg("users");

    memset(userConfig.ssid, 0, sizeof(userConfig.ssid));
    memset(userConfig.password, 0, sizeof(userConfig.password));
    memset(userConfig.botToken, 0, sizeof(userConfig.botToken));
    memset(userConfig.authorizedUsers, 0, sizeof(userConfig.authorizedUsers));

    strncpy(userConfig.ssid, newSsid.c_str(), sizeof(userConfig.ssid) - 1);
    strncpy(userConfig.password, newPass.c_str(), sizeof(userConfig.password) - 1);
    strncpy(userConfig.botToken, newToken.c_str(), sizeof(userConfig.botToken) - 1);
    strncpy(userConfig.authorizedUsers, newUsers.c_str(), sizeof(userConfig.authorizedUsers) - 1);

    userConfig.configured = true;
    saveConfig();
    server.send(200, "text/html", "<h1>Configurazione Salvata!</h1><p>Il dispositivo si riavvia...</p>");
    delay(2000);
    ESP.restart();
  } 
  else {
    server.send(400, "text/plain", "Parametri mancanti.");
  }
}

/**
 * @brief Gestore per le richieste non trovate.
 *        Reindirizza sempre alla root `/`.
 */
void handleNotFound() {
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "");
}

// ======= PAGINA CAMBIO PASSWORD CONFIGURAZIONE =======
/**
 * @brief Pagina per cambiare la password di accesso alla configurazione.
 */
void handleChangePassword() {
  String html = R"rawliteral(
    <!DOCTYPE HTML>
    <html>
    <head>
      <title>Cambia Password</title>
      <meta name="viewport" content="width=device-width, initial-scale=1">
      <style>
        button, input[type=submit] {
          background-color: #2196F3;
          color: white;
          font-size: 12px;
          padding: 10px 20px;
          border: none;
          border-radius: 5px;
          margin-top: 10px;
          cursor: pointer;
        }
        button:active, input[type=submit]:active {
          background-color: #1769aa;
        }
        a.button-link {
          display: inline-block;
          background-color: #2196F3;
          color: white !important;
          font-size: 12px;
          padding: 10px 20px;
          border: none;
          border-radius: 5px;
          margin-top: 10px;
          text-decoration: none;
          cursor: pointer;
        }
        a.button-link:active {
          background-color: #1769aa;
        }
      </style>
    </head>
    <body>
      <h1>Cambia Password Configurazione</h1>
      <form action="/changepw" method="post" onsubmit="return checkPwMatch();">
        <label for="oldpw">Password attuale:</label><br>
        <input type="password" id="oldpw" name="oldpw" required><br><br>
        <label for="newpw">Nuova password:</label><br>
        <input type="password" id="newpw" name="newpw" required><br><br>
        <label for="newpw2">Ripeti nuova password:</label><br>
        <input type="password" id="newpw2" name="newpw2" required><br><br>
        <input type="submit" value="Cambia Password">
      </form>
      <br>
      <a href="/" class="button-link">Torna alla configurazione</a>
      <script>
        function checkPwMatch() {
          var pw1 = document.getElementById('newpw').value;
          var pw2 = document.getElementById('newpw2').value;
          if (pw1 !== pw2) {
            alert('Le nuove password non coincidono!');
            return false;
          }
          return true;
        }
      </script>
    </body>
    </html>
  )rawliteral";
  server.send(200, "text/html", html);
}

/**
 * @brief Gestore POST per modificare la password di configurazione.
 */
void handleChangePasswordPost() {
  if (!server.hasArg("oldpw") || !server.hasArg("newpw") || !server.hasArg("newpw2")) {
    server.send(400, "text/plain", "Parametri mancanti.");
    return;
  }

  String oldpw = server.arg("oldpw");
  String newpw = server.arg("newpw");
  String newpw2 = server.arg("newpw2");

  if (strcmp(oldpw.c_str(), userConfig.configPassword) != 0) {
    server.send(403, "text/html", "<h1>Password attuale errata!</h1>");
    return;
  }

  if (newpw != newpw2) {
    server.send(400, "text/html", "<h1>Le nuove password non coincidono!</h1><a href=\"/changepw\" class=\"button-link\">Riprova</a>");
    return;
  }

  strncpy(userConfig.configPassword, newpw.c_str(), MAX_PASSCFG_LEN-1);
  saveConfig();

  server.send(200, "text/html", "<h1>Password aggiornata!</h1><a href=\"/\" class=\"button-link\">Torna alla configurazione</a>");
}

// ========== SETUP ACCESS POINT E SERVER ==========
/**
 * @brief Configura l'ESP8266 in modalità Access Point (AP).
 *        Avvia il server web e il DNS per la configurazione iniziale.
 */
void setupAP() {
  passwordOk = false; // Richiedi sempre la password all'ingresso in config!
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP_Config", "");
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  server.on("/", handleRoot);
  server.on("/login", HTTP_POST, handleLogin);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/changepw", HTTP_GET, handleChangePassword);
  server.on("/changepw", HTTP_POST, handleChangePasswordPost);
  server.on("/reset_eeprom", HTTP_POST, handleResetEEPROM); // <-- handler reset
  server.onNotFound(handleNotFound);

  server.begin();

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  drawCenteredText("MODALITA' CONFIG", 0, 1);
  drawCenteredText("SSID: ESP_Config", 16, 1);
  drawCenteredText("Apri 192.168.4.1", 32, 1);
  display.display();
}

// =================== WIFI DINAMICO =======================
/**
 * @brief Prova a connettersi alla rete WiFi configurata.
 *        Tenta per un massimo di 30 secondi (30 tentativi da 1s).
 *        Aggiorna lo stato WiFi e il display.
 */
bool connectToWiFiAndCheck() {
  if (!userConfig.configured) return false; // Non configurato: impossibile

  WiFi.mode(WIFI_STA); // Imposta ESP8266 in modalità Station
  WiFi.begin(userConfig.ssid, userConfig.password); // Avvia connessione

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(1000); // Aspetta 1 secondo
    attempts++;  // Incrementa contatore tentativi
  }

  wifiOk = (WiFi.status() == WL_CONNECTED); // Aggiorna stato WiFi
  updateDisplay(); // Aggiorna il display OLED

  if (wifiOk) {
    Serial.println("\nWiFi connesso!");
    Serial.print("Indirizzo IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Intensità segnale: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    successBeep();
  } else {
    Serial.println("\nErrore connessione WiFi!");
    errorBeep();
  }

  return wifiOk;
} 



// ======================== SETUP ==========================
/**
 * @brief Funzione di setup iniziale dell'ESP8266.
 *        Inizializza:
 *        - Seriale
 *        - Display OLED
 *        - Configurazione da EEPROM
 *        - Parsing degli utenti autorizzati
 *        - Pin I/O
 *        - Connessione WiFi
 *        - Bot Telegram
 *        - Logging
 *        - Test del bot Telegram
 *        - Melodia iniziale
 */
void setup() {
  // ====== INIZIALIZZAZIONE SERIALE ======
  Serial.begin(115200); // Velocità di comunicazione seriale
  //delay(1500);

  // ====== BENVENUTO SU SERIALE ======
  Serial.println("\n\n=================================");
  Serial.println("=== D1 mini Telegram Citofono ===");
  Serial.println("=================================\n");
  Serial.println("Inizializzazione sistema...\n");

  // ====== INIZIALIZZAZIONE DISPLAY OLED ======
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // 0x3C è l'indirizzo I2C del display
    Serial.println(F("SSD1306 non trovato!")); // Errore se il display non risponde
  }
  display.clearDisplay(); // Pulisce il display
  display.display(); // Mostra lo schermo 

  // ====== CARICA CONFIGURAZIONE DALLA EEPROM ======
  loadConfig(); // Carica SSID, password, token bot, ecc.

  // ====== PARSE UTENTI AUTORIZZATI ======
  parseAuthorizedUsers(); // Converte la stringa in array di chat ID

  // ====== CONFIGURAZIONE DEI PIN ======
  pinMode(RELAY_PORTA, OUTPUT); // Relè per aprire la porta
  pinMode(RELAY_LUCE, OUTPUT);  // Relè per accendere la luce
  pinMode(LED_STATUS, OUTPUT); // LED blu per stato
  pinMode(BUZZER_PIN, OUTPUT); // Buzzer per feedback sonoro
  pinMode(BUTTON_PORTA, INPUT_PULLUP); // Pulsante fisico apriporta con pull-up
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP); // Pulsante reset configurazione

  // ====== ATTACH INTERRUPT PER IL PULSANTE ======
  attachInterrupt(digitalPinToInterrupt(BUTTON_PORTA), buttonISR, FALLING); // Attiva interrupt su fronte discendente
  
  // ====== RESET DEI RELÈ E OUTPUT ======
  digitalWrite(RELAY_PORTA, LOW);  // Assicura che la porta sia chiusa
  digitalWrite(RELAY_LUCE, LOW);   // Assicura che la luce sia spenta
  digitalWrite(LED_STATUS, LOW);  // Spegni LED status
  digitalWrite(BUZZER_PIN, LOW);  // Spegni buzzer

  // ====== CONNESSIONE A WIFI ======
  if (!userConfig.configured || !connectToWiFiAndCheck()) {
    // Se non configurato o errore WiFi, entra in modalità configurazione
    setupAP();
    while (true) {
      dnsServer.processNextRequest(); // Gestore DNS
      server.handleClient(); // Gestore HTTP
      delay(10); // Piccola pausa per non sovraccaricare il processore
    }
  }

  // ====== CONFIGURA IL BOT TELEGRAM ======
  bot = UniversalTelegramBot(userConfig.botToken, client); // Imposta il token del bot
  client.setInsecure(); // Disabilita controllo certificati per compatibilità

  // ====== INIZIALIZZA IL LOG ======
  initializeLog(); // Azzera il registro delle operazioni

  // ====== TEST DEL BOT TELEGRAM ======
  testTelegramBot(); // Verifica la connessione al bot

  // ====== MESSAGGIO DI SISTEMA PRONTO ======
  systemReady(); // Mostra stato iniziale e suona la melodia di avvio

  // ====== AGGIORNA IL DISPLAY ======
  updateDisplay(); // Mostra stato iniziale sul display OLED
}

// ======================== LOOP ===========================
/**
 * @brief Funzione principale eseguita in loop.
 *        Gestisce:
 *        - Modalità configurazione manuale (pulsante reset)
 *        - Polling Telegram
 *        - Timeout porte/luci
 *        - Lampeggio LED di stato
 *        - Controllo connessione WiFi
 *        - Pulsante fisico apriporta
 *        - Aggiornamento display OLED
 */
void loop() {
  // ====== MODALITÀ CONFIGURAZIONE MANUALE ======
  /**
   * Se il pulsante di reset è premuto per più di 3 secondi,
   * entra in modalità configurazione web.
   */
  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    if (resetButtonPressTime == 0) {
      resetButtonPressTime = millis();
    } else if (millis() - resetButtonPressTime > 3000 && !configModeRequested) {
      configModeRequested = true;

      // Segnale acustico di conferma
      tone(BUZZER_PIN, 1000, 500);
      delay(600);
      tone(BUZZER_PIN, 1500, 500);
      delay(600);
      noTone(BUZZER_PIN);

      // Avvia modalità configurazione
      setupAP();
      while (true) {
        dnsServer.processNextRequest(); // Gestore DNS
        server.handleClient();          // Server web
        delay(10);                      // Piccola pausa per non sovraccaricare
      }
    }
  } else {
    // Resetta lo stato del pulsante quando viene rilasciato
    resetButtonPressTime = 0;
    configModeRequested = false;
  }

  // ====== POLLING MESSAGGI TELEGRAM ======
  if (millis() > lastTimeBotRan + BOT_REQUEST_DELAY) {
    checkTelegramMessages();
    lastTimeBotRan = millis();
  }

  // ====== GESTIONE TIMEOUT AUTOMATICI ======
  handleTimeouts(); // Chiude la porta o spegne la luce dopo timeout

  // ====== LAMPEGGIO LED DI STATO ======
  blinkStatusLED(); // LED status lampeggia regolarmente

  // ====== CONTROLLO CONNESSIONE WIFI ======
  checkWiFiConnection(); // Verifica periodicamente la connessione

  // ====== CONTROLLO PULSANTE FISICO APRIPORTA ======
  checkButtonApriPorta(); // Verifica se è stato premuto il pulsante

  // ====== AGGIORNAMENTO DISPLAY OLED ======
  updateDisplay(); // Mostra stato corrente sul display
}

// ==================== FUNZIONI SETUP =====================
/**
 * @brief Configura tutti i pin utilizzati come input o output.
 *        Imposta i relè, LED, pulsanti e attacca l'interrupt per il pulsante fisico.
 */
void setupPins() {
  // ====== CONFIGURAZIONE DEI PIN ======
  pinMode(RELAY_PORTA, OUTPUT); // Relè per aprire la porta
  pinMode(RELAY_LUCE, OUTPUT);  // Relè per accendere la luce
  pinMode(LED_STATUS, OUTPUT); // LED blu per stato
  pinMode(BUZZER_PIN, OUTPUT); // Buzzer per feedback sonoro
  pinMode(BUTTON_PORTA, INPUT_PULLUP); // Pulsante fisico apriporta con pull-up
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP); // Pulsante reset configurazione


  // ====== ATTACH INTERRUPT PER IL PULSANTE ======
  attachInterrupt(digitalPinToInterrupt(BUTTON_PORTA), buttonISR, FALLING); // Attiva interrupt su fronte discendente

  // ====== RESET DEI RELÈ E OUTPUT ======
  digitalWrite(RELAY_PORTA, LOW);  // Assicura che la porta sia chiusa
  digitalWrite(RELAY_LUCE, LOW);   // Assicura che la luce sia spenta
  digitalWrite(LED_STATUS, LOW);  // Spegni LED status
  digitalWrite(BUZZER_PIN, LOW);  // Spegni buzzer

  Serial.println("Pin configurati");
}

/**
 * @brief Testa la connessione al bot Telegram per verificare se il token è valido.
 *        Invia un segnale acustico positivo o negativo in base all'esito.
 */
void testTelegramBot() {
  Serial.print("Test connessione bot Telegram...");
  if (bot.getMe()) {
    Serial.println(" OK!");
    Serial.println("Bot pronto per ricevere comandi");
    telegramOk = true;
    successBeep();
  } else {
    Serial.println(" ERRORE!");
    Serial.println("Verifica il token del bot");
    telegramOk = false;
    errorBeep();
  }
  updateDisplay();
}

/**
 * @brief Funzione eseguita una volta all'avvio del sistema.
 *        Mostra messaggio di benvenuto su seriale e su display OLED.
 *        Riproduce una melodia iniziale.
 */
void systemReady() {
  Serial.println("\n======== SISTEMA PRONTO ========");
  Serial.println("Comandi disponibili da inserire");
  Serial.println("nel BOT di Telegram:");
  Serial.println("- /apri  -> Apre la porta");
  Serial.println("- /luce  -> Accende luce scale");
  Serial.println("- /stato -> Mostra stato sistema");
  Serial.println("- /log   -> Mostra log operazioni");
  Serial.println("- /help  -> Mostra aiuto");
  Serial.println("================================\n");

  digitalWrite(LED_STATUS, HIGH);
  playStartupMelody();
}

// =============== GESTIONE MESSAGGI TELEGRAM ==============
/**
 * @brief Controlla se ci sono nuovi messaggi Telegram.
 *        Invia una richiesta al bot e aggiorna lo stato di connessione.
 */
void checkTelegramMessages() {
  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  telegramOk = (numNewMessages >= 0);
  updateDisplay();
  while (numNewMessages) {
    handleNewMessages(numNewMessages);
    numNewMessages = bot.getUpdates(bot.last_message_received + 1);
    telegramOk = (numNewMessages >= 0);
    updateDisplay();
  }
}

/**
 * @brief Gestisce i nuovi messaggi ricevuti dal bot Telegram.
 *        Verifica l'autorizzazione dell'utente e chiama il processore di comandi.
 */
void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    if (bot.messages[i].type == "message") {
      String chat_id = bot.messages[i].chat_id;
      String text = bot.messages[i].text;
      String from_name = bot.messages[i].from_name;
      long user_id = chat_id.toInt();

      if (!isAuthorizedUser(user_id)) {
        handleUnauthorizedUser(chat_id, from_name);
        continue;
      }
      processCommand(chat_id, text, from_name, user_id);
    } else if (bot.messages[i].type == "callback_query") {
      String chat_id = bot.messages[i].chat_id;
      String callback_data = bot.messages[i].text;
      String from_name = bot.messages[i].from_name;
      String callback_query_id = bot.messages[i].query_id;
      long user_id = chat_id.toInt();

      if (!isAuthorizedUser(user_id)) {
        bot.answerCallbackQuery(callback_query_id, "❌ Non autorizzato");
        continue;
      }
      processCallback(chat_id, callback_data, from_name, user_id, callback_query_id);
    }
  }
}

/**
 * @brief Processa un comando testuale ricevuto da Telegram.
 */
void processCommand(String chat_id, String text, String from_name, long user_id) {
  if (text == "/start") {
    sendWelcomeMessage(chat_id);
  }
  else if (text == "/apri") {
    handleApriPorta(chat_id, from_name, user_id);
  }
  else if (text == "/luce") {
    handleAccendiLuce(chat_id, from_name, user_id);
  }
  else if (text == "/stato") {
    handleStatoSistema(chat_id);
  }
  else if (text == "/log") {
    handleMostraLog(chat_id);
  }
  else if (text == "/help") {
    sendHelpMessage(chat_id);
  }
  else if (text == "/menu") {
    sendMainMenu(chat_id);
  }
  else {
    handleUnknownCommand(chat_id);
  }
}

/**
 * @brief Processa un comando inviato tramite pulsanti inline Telegram.
 */
void processCallback(String chat_id, String callback_data, String from_name, long user_id, String query_id) {
  if (callback_data == "APRI_PORTA") {
    handleApriPortaCallback(chat_id, from_name, user_id, query_id);
  }
  else if (callback_data == "ACCENDI_LUCE") {
    handleAccendiLuceCallback(chat_id, from_name, user_id, query_id);
  }
  else if (callback_data == "STATO_SISTEMA") {
    handleStatoSistemaCallback(chat_id, query_id);
  }
  else if (callback_data == "MOSTRA_LOG") {
    handleMostraLogCallback(chat_id, query_id);
  }
  else if (callback_data == "AIUTO") {
    handleAiutoCallback(chat_id, query_id);
  }
  else {
    bot.answerCallbackQuery(query_id, "❌ Comando non riconosciuto");
  }
}

// ===================== GESTIONE COMANDI ==================
/**
 * @brief Invia un messaggio di benvenuto all'utente che usa `/start`.
 */
void sendWelcomeMessage(String chat_id) {
  String welcome = "🏠 *Benvenuto nel Sistema Citofono*\n\n";
  welcome += "Il sistema è operativo e pronto all'uso.\n\n";
  welcome += "Usa i pulsanti qui sotto per controllare il sistema o digita i comandi manualmente.\n\n";
  welcome += "*Comandi testuali disponibili:*\n";
  welcome += "🚪 /apri - Apre la porta d'ingresso\n";
  welcome += "💡 /luce - Accende luce delle scale\n";
  welcome += "ℹ️ /stato - Stato del sistema\n";
  welcome += "📋 /log - Registro operazioni\n";
  welcome += "🎛️ /menu - Mostra menu pulsanti\n";
  welcome += "❓ /help - Guida completa\n\n";
  welcome += "_Sistema sicuro con controllo accessi_";

  bot.sendMessage(chat_id, welcome, "Markdown");
  delay(500);
  sendMainMenu(chat_id);
}

// =================== GESTIONE APRIPORTA ==================
/**
 * @brief Apri la porta e registra l'operazione.
 */
void handleApriPorta(String chat_id, String from_name, long user_id) {
  apriPorta();

  String message = "🚪 *Porta Aperta*\n\n";
  message += "✅ Comando eseguito con successo\n";
  message += "⏰ La porta è stata APERTA\n";
  message += "👤 Richiesta da: " + from_name;
  bot.sendMessage(chat_id, message, "Markdown");
  logOperation(user_id, "PORTA_APERTA", from_name);
  updateDisplay();

  Serial.println("✅ Porta aperta su richiesta di: " + from_name);
  confirmationBeep();

  delay(2000);
  sendMainMenu(chat_id);
}

// ================= GESTIONE ACCENDI LUCE =================
/**
 * @brief Accendi la luce scale e registra l'operazione.
 */
void handleAccendiLuce(String chat_id, String from_name, long user_id) {
  accendiLuce();

  String message = "💡 *Luce Accesa*\n\n";
  message += "✅ Luce delle scale accesa\n";
  message += "⏰ Si spegnerà automaticamente tra 30 secondi\n";
  message += "👤 Richiesta da: " + from_name;

  bot.sendMessage(chat_id, message, "Markdown");
  logOperation(user_id, "LUCE_ACCESA", from_name);
  updateDisplay();

  Serial.println("✅ Luce accesa su richiesta di: " + from_name);
  confirmationBeep();

  delay(2000);
  sendMainMenu(chat_id);
}

// =========== GESTIONE PULSANTE FISICO APRIPORTA ==========
/**
 * @brief Controlla se è stato premuto il pulsante fisico per aprire la porta.
 */
void checkButtonApriPorta() {
  if (buttonInterruptFlag) {
    buttonInterruptFlag = false;
    apriPortaDaPulsante();
  }
}

// ================= APRIPORTA DA PULSANTE =================
/**
 * @brief Aziona la porta tramite il pulsante fisico e registra l'evento.
 */
void apriPortaDaPulsante() {
  apriPorta();
  logOperation(0, "PORTA_PULSANTE", "Pulsante");
  //portaApertaDaPulsante = true;
  Serial.println("✅ Porta aperta da pulsante fisico");
  confirmationBeep();
  updateDisplay();
}

// ============ VISUALIZZAZIONE STATO DI SISTEMA ===========
/**
 * @brief Gestisce il comando `/stato` inviando tramite Telegram lo stato completo del sistema.
 *        Richiama la funzione `getDetailedSystemStatus()` per ottenere i dati formattati.
 */
void handleStatoSistema(String chat_id) {
  String status = getDetailedSystemStatus();
  bot.sendMessage(chat_id, status, "Markdown");
  delay(1000);
  sendMainMenu(chat_id);
}

// ====================== MOSTRA LOG =======================
/**
 * @brief Gestisce il comando `/log` inviando tramite Telegram il registro delle ultime operazioni.
 *        Richiama la funzione `getFormattedLog()` per ottenere i dati formattati.
 */
void handleMostraLog(String chat_id) {
  String logStr = getFormattedLog();
  bot.sendMessage(chat_id, logStr, "Markdown");
  delay(1000);
  sendMainMenu(chat_id);
}

// =================== MESSAGGIO DI HELP ===================
/**
 * @brief Gestisce il comando `/help` mostrando una guida completa dei comandi disponibili.
 *        Include informazioni sui comandi principali, informativi, sulla sicurezza e sull'uso del sistema.
 */
void sendHelpMessage(String chat_id) {
  String help = "❓ *Guida Comandi Sistema Citofono*\n\n";
  help += "*Comandi Principali:*\n";
  help += "🚪 `/apri` - Apre la porta d'ingresso\n";
  help += "💡 `/luce` - Accende luce scale\n";
  help += "*Comandi Informativi:*\n";
  help += "ℹ️ `/stato` - Stato completo del sistema\n";
  help += "📋 `/log` - Ultimi log operazioni\n";
  help += "❓ `/help` - Questa guida\n\n";
  help += "*Sicurezza:*\n";
  help += "🔒 Solo utenti autorizzati possono usare il sistema\n";
  help += "📊 Tutte le operazioni vengono registrate\n";
  help += "🔄 Spegnimento automatico per sicurezza";

  bot.sendMessage(chat_id, help, "Markdown");
  delay(1000);
  sendMainMenu(chat_id);
}

// ================== COMANDO SCONOSCIUTO ==================
/**
 * @brief Risponde a un comando non riconosciuto fornendo indicazioni su come procedere.
 *        Suggerisce l'uso di `/help` o `/menu`.
 */
void handleUnknownCommand(String chat_id) {
  String message = "❓ *Comando Non Riconosciuto*\n\n";
  message += "Il comando inserito non è valido.\n";
  message += "Usa /help per vedere tutti i comandi disponibili o /menu per i pulsanti.";
  bot.sendMessage(chat_id, message, "Markdown");
}

// ================ CONTROLLO AUTORIZZAZIONI ===============
/**
 * @brief Gestisce gli accessi non autorizzati al sistema.
 *        Invia un messaggio di errore e un segnale acustico.
 */
void handleUnauthorizedUser(String chat_id, String from_name) {
  String message = "🚫 *Accesso Negato*\n\n";
  message += "Non sei autorizzato ad utilizzare questo sistema.\n";
  message += "Contatta l'amministratore per richiedere l'accesso.";
  bot.sendMessage(chat_id, message, "Markdown");
  Serial.println("🚫 Accesso negato per: " + from_name + " (ID: " + chat_id + ")");
  errorBeep();
}

// ==================== MENU E CALLBACK ====================
/**
 * @brief Invia un menu principale con pulsanti interattivi per controllare il sistema via Telegram.
 *        Permette di:
 *        - Aprire la porta
 *        - Accendere la luce
 *        - Visualizzare lo stato del sistema
 *        - Mostrare il registro delle operazioni
 *        - Accedere alla guida
 */
void sendMainMenu(String chat_id) {
  String menuText = "🎛️ *Menu Controllo Citofono*\n\n";
  menuText += "Scegli un'azione usando i pulsanti qui sotto:";
  String keyboard = "[[{\"text\":\"🚪 Apri Porta\",\"callback_data\":\"APRI_PORTA\"}],"
                    "[{\"text\":\"💡 Accendi Luce\",\"callback_data\":\"ACCENDI_LUCE\"}],"
                    "[{\"text\":\"ℹ️ Stato Sistema\",\"callback_data\":\"STATO_SISTEMA\"},"
                    "{\"text\":\"📋 Mostra Log\",\"callback_data\":\"MOSTRA_LOG\"}],"
                    "[{\"text\":\"❓ Aiuto\",\"callback_data\":\"AIUTO\"}]]";
  bot.sendMessageWithInlineKeyboard(chat_id, menuText, "Markdown", keyboard);
}

/**
 * @brief Gestisce il callback del pulsante "Apri Porta" nel menu Telegram.
 *        Apri la porta e registra l'operazione nel log.
 */
void handleApriPortaCallback(String chat_id, String from_name, long user_id, String query_id) {
  apriPorta();
  bot.answerCallbackQuery(query_id, "🚪 Porta Aperta! Apriporta Azionato!");
  String message = "🚪 *Porta Aperta*\n\n";
  message += "✅ Comando eseguito con successo\n";
  message += "⏰ La porta è stata aperta!\n";
  message += "👤 Richiesta da: " + from_name;
  bot.sendMessage(chat_id, message, "Markdown");
  logOperation(user_id, "PORTA_APERTA", from_name);
  updateDisplay();
  Serial.println("✅ Porta aperta su richiesta di: " + from_name);
  confirmationBeep();
  sendMainMenu(chat_id);
}

/**
 * @brief Gestisce il callback del pulsante "Accendi Luce" nel menu Telegram.
 *        Accende la luce e registra l'operazione nel log.
 */
void handleAccendiLuceCallback(String chat_id, String from_name, long user_id, String query_id) {
  accendiLuce();
  bot.answerCallbackQuery(query_id, "💡 Luce Accesa! Si spegnerà automaticamente");
  String message = "💡 *Luce Accesa*\n\n";
  message += "✅ Luce delle scale accesa\n";
  message += "⏰ Si spegnerà automaticamente!\n";
  message += "👤 Richiesta da: " + from_name;
  bot.sendMessage(chat_id, message, "Markdown");
  logOperation(user_id, "LUCE_ACCESA", from_name);
  updateDisplay();
  Serial.println("✅ Luce accesa su richiesta di: " + from_name);
  confirmationBeep();
  delay(1000);
  sendMainMenu(chat_id);
}

/**
 * @brief Gestisce il callback del pulsante "Stato Sistema" nel menu Telegram.
 *        Mostra lo stato completo del sistema.
 */
void handleStatoSistemaCallback(String chat_id, String query_id) {
  bot.answerCallbackQuery(query_id, "📊 Recupero stato sistema...");
  String status = getDetailedSystemStatus();
  bot.sendMessage(chat_id, status, "Markdown");
  delay(1000);
  sendMainMenu(chat_id);
}

/**
 * @brief Gestisce il callback del pulsante "Mostra Log" nel menu Telegram.
 *        Mostra le ultime operazioni registrate nel sistema.
 */
void handleMostraLogCallback(String chat_id, String query_id) {
  bot.answerCallbackQuery(query_id, "📋 Recupero log operazioni...");
  String logStr = getFormattedLog();
  bot.sendMessage(chat_id, logStr, "Markdown");
  delay(1000);
  sendMainMenu(chat_id);
}

/**
 * @brief Gestisce il callback del pulsante "Aiuto" nel menu Telegram.
 *        Richiama la guida completa sui comandi del sistema.
 */
void handleAiutoCallback(String chat_id, String query_id) {
  bot.answerCallbackQuery(query_id, "❓ Guida ai comandi...");
  sendHelpMessage(chat_id);
}



// ================ FUNZIONI DI LOGGING DELLE OPERAZIONI ================
/**
 * @brief Inizializza il registro delle operazioni con valori vuoti.
 */
 void initializeLog() {
  for (int i = 0; i < 5; i++) {
    operationLog[i].timestamp = 0;
    operationLog[i].chatId = 0;
    operationLog[i].operation = "";
    operationLog[i].userName = "";
  }
}

/**
 * @brief Registra una nuova operazione nel registro delle operazioni (log).
 *        Mantiene le ultime 5 operazioni, spostando in basso quelle meno recenti
 *        e aggiungendo la nuova operazione in cima al log.
 *
 * @param chatId   ID della chat Telegram dell'utente che ha eseguito l'operazione
 * @param operation Stringa che descrive il tipo di operazione (es. "PORTA_APERTA")
 * @param userName Nome dell'utente che ha effettuato l'operazione
 */
void logOperation(long chatId, String operation, String userName) {
  // Sposta ogni elemento del log verso il basso di una posizione,
  for (int i = 4; i > 0; i--) {
    operationLog[i] = operationLog[i - 1];
  }

  // Inserisce la nuova operazione nella prima posizione del log
  operationLog[0].timestamp = millis();     // Timestamp corrente (in millisecondi)
  operationLog[0].chatId = chatId;         // Chat ID dell'utente che ha eseguito l'azione
  operationLog[0].operation = operation;   // Tipo di operazione (es. "PORTA_APERTA")
  operationLog[0].userName = userName;     // Nome dell'utente che ha eseguito l'azione

  // Aggiorna il display OLED per mostrare il log aggiornato
  updateDisplay();
}

/**
 * @brief Restituisce una stringa formattata con il registro delle operazioni,
 *        utile per mostrare il log su Telegram o sul display.
 */
String getFormattedLog() {
  String logStr = "📋 *Registro Operazioni*\n\n";
  int validEntries = 0;

  // Conta quante operazioni sono valide
  for (int i = 0; i < 5; i++) {
    if (operationLog[i].timestamp > 0) validEntries++;
  }

  // Se non ci sono operazioni registrate
  if (validEntries == 0) {
    logStr += "Nessuna operazione registrata.";
    return logStr;
  }

  // Formatta le operazioni per mostrare data/ora, tipo, nome utente
  for (int i = 0; i < validEntries; i++) {
    String timeAgo = formatTimeAgo(millis() - operationLog[i].timestamp);
    String emoji = "🚪";
    String operationName = "PORTA APERTA";

    // Cambia emoji e nome se è la luce
    if (operationLog[i].operation == "LUCE_ACCESA") {
      emoji = "💡";
      operationName = "LUCE ACCESA";
    }
    logStr += "• " + emoji + " " + operationName + "\n";
    logStr += "  👤 " + operationLog[i].userName + "\n";
    logStr += "  ⏰ " + timeAgo + "\n\n";
  }
  return logStr;
}

// ================ FUNZIONI PER STATO DEL SISTEMA =================
/**
 * @brief Restituisce una stringa formattata con lo stato completo del sistema.
 *        Usata per il comando `/stato` e il menu principale.
 */
String getDetailedSystemStatus() {
  String status = "ℹ️ *Stato Sistema Citofono*\n\n";

  // Stato connessione WiFi
  status += "*Connettività:*\n";
  status += "📶 WiFi: " + String(WiFi.status() == WL_CONNECTED ? "✅ Connesso" : "❌ Disconnesso") + "\n";
  status += "📡 Segnale: " + String(WiFi.RSSI()) + " dBm\n\n";

  // Stato dispositivi (porta e luce)
  status += "*Dispositivi:*\n";
  status += "🚪 Porta: " + String(portaOpenTime > 0 ? "🟢 Aperta" : "🔴 Chiusa") + "\n";
  status += "💡 Luce: " + String(luceOnTime > 0 ? "🟢 Accesa" : "🔴 Spenta") + "\n\n";

  // Informazioni di sistema
  status += "*Sistema:*\n";
  status += "⏱️ Uptime: " + formatUptime(millis()) + "\n";
  status += "🔋 Memoria libera: " + String(ESP.getFreeHeap()) + " bytes\n";
  status += "👥 Utenti autorizzati: " + String(numAuthorizedUsers);

  return status;
}

/**
 * @brief Formatta il tempo trascorso in un formato leggibile.
 *        Es: "1 giorno 3h 45m 20s"
 */
String formatUptime(unsigned long milliseconds) {
  unsigned long seconds = milliseconds / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  unsigned long days = hours / 24;

  String uptime = "";

  if (days > 0) uptime += String(days) + "g ";
  if (hours % 24 > 0) uptime += String(hours % 24) + "h ";
  if (minutes % 60 > 0) uptime += String(minutes % 60) + "m ";
  uptime += String(seconds % 60) + "s";

  return uptime;
}

/**
 * @brief Formatta un intervallo temporale in formato leggibile.
 *        Es: "3 minuti fa", "2 ore fa", ecc.
 */
String formatTimeAgo(unsigned long ago) {
  unsigned long seconds = ago / 1000;
  if (seconds < 60) return String(seconds) + " secondi fa";
  unsigned long minutes = seconds / 60;
  if (minutes < 60) return String(minutes) + " minuti fa";
  unsigned long hours = minutes / 60;
  if (hours < 24) return String(hours) + " ore fa";
  unsigned long days = hours / 24;
  return String(days) + " giorni fa";
}

// =================== CONTROLLO RELÈ E TIMEOUT ===================
/**
 * @brief Apre la porta attivando il relè corrispondente.
 *        Imposta il timestamp dell'ultima apertura.
 */
void apriPorta() {
  digitalWrite(RELAY_PORTA, HIGH);
  portaOpenTime = millis();
  Serial.println("🚪 Porta aperta");
}

/**
 * @brief Accende la luce scale attivando il relè corrispondente.
 *        Imposta il timestamp dell'ultima accensione.
 */
void accendiLuce() {
  digitalWrite(RELAY_LUCE, HIGH);
  luceOnTime = millis();
  Serial.println("💡 Luce accesa");
}

/**
 * @brief Gestisce i timeout automatici:
 *        - Chiude la porta dopo un certo periodo
 *        - Spegne la luce dopo un certo periodo
 */
void handleTimeouts() {
  // ====== TIMEOUT PORTA ======
  if (portaOpenTime > 0 && millis() - portaOpenTime > PORTA_TIMEOUT) {
    digitalWrite(RELAY_PORTA, LOW);
    portaOpenTime = 0;
    Serial.println("🚪 Porta chiusa automaticamente");
  }

  // ====== TIMEOUT LUCE ======
  if (luceOnTime > 0 && millis() - luceOnTime > LUCE_TIMEOUT) {
    digitalWrite(RELAY_LUCE, LOW);
    luceOnTime = 0;
    Serial.println("💡 Luce spenta automaticamente");
  }
}

// =============== CONTROLLO AUTORIZZAZIONE UTENTI ==============
/**
 * @brief Verifica se un chat ID è nella lista degli utenti autorizzati.
 *        Utile per limitare l'accesso ai comandi Telegram.
 */
bool isAuthorizedUser(long chat_id) {
  Serial.print("Verifica autorizzazione per chat_id: ");
  Serial.println(chat_id);

  for (int i = 0; i < numAuthorizedUsers; i++) {
    Serial.print("Controllo contro: ");
    Serial.println(authorizedUsers[i]);

    if (authorizedUsers[i] == chat_id) {
      return true; // Utente trovato
    }
  }
  return false; // Utente non autorizzato
}
// ================= FEEDBACK SONORO E MELODIE =================
/**
 * @brief Riproduce un breve "beep" positivo per confermare l'esecuzione corretta di un'azione.
 */
void confirmationBeep() {
  int melody[] = {587, 659, 523, 262, 392};
  int duration[] = {200, 200, 200, 200, 800};

  for (int i = 0; i < 5; i++) {
    tone(BUZZER_PIN, melody[i], duration[i]);
    delay(250);
  }

  noTone(BUZZER_PIN);
}

/**
 * @brief Segnale acustico di successo (es. connessione WiFi riuscita).
 */
void successBeep() {
  int melody[] = {262, 330, 392}; // Note: Do, Mi, Sol
  int duration[] = {200, 200, 200};

  for (int i = 0; i < 3; i++) {
    tone(BUZZER_PIN, melody[i], duration[i]);
    delay(250);
  }

  noTone(BUZZER_PIN);
  delay(2000);
}

/**
 * @brief Segnale acustico di errore (es. password errata, accesso negato).
 */
void errorBeep() {
  tone(BUZZER_PIN, 1000);
  delay(100);
  noTone(BUZZER_PIN);
  delay(80);
  tone(BUZZER_PIN, 1500);
  delay(100);
  noTone(BUZZER_PIN);
}

/**
 * @brief Riproduce una melodia all'avvio del sistema.
 *        Utile come feedback che il dispositivo è pronto.
 */
void playStartupMelody() {
  int melody[] = {587, 659, 523, 262, 392};
  int duration[] = {200, 200, 200, 200, 800};
  for (int i = 0; i < 5; i++) {
    tone(BUZZER_PIN, melody[i], duration[i]);
    delay(250);
  }
  noTone(BUZZER_PIN);
}

// ==================== LED DI STATO ====================
/**
 * @brief Lampeggia il LED blu integrato sull'ESP8266 per indicare che il sistema è attivo.
 *        Il LED lampeggia ogni secondo circa.
 */
void blinkStatusLED() {
  static unsigned long lastBlink = 0;
  static bool ledState = false;
  if (millis() - lastBlink > 1000) {
    ledState = !ledState;
    digitalWrite(LED_STATUS, ledState ? HIGH : LOW);
    lastBlink = millis();
  }
}

// ================= CONTROLLO CONNESSIONE =================
/**
 * @brief Controlla periodicamente la connessione WiFi e tenta la riconnessione se necessario.
 *        Aggiorna lo stato del display OLED.
 */
void checkWiFiConnection() {
  static unsigned long lastCheck = 0;

  if (millis() - lastCheck > 30000) { // Ogni 30 secondi
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi disconnesso, riconnessione...");
      WiFi.reconnect(); // Tenta di riconnettersi
      wifiOk = false; // Aggiorna stato connessione
    } else {
      wifiOk = true; // Connessione OK
    }

    updateDisplay(); // Aggiorna il display OLED
    lastCheck = millis(); // Aggiorna timestamp dell'ultimo controllo
  }
}




