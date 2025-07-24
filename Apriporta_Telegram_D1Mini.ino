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
 *    la password predefinita "admin".
 *    Inserire SSID e BOT_TOKEN.
 *    inserire gli ID utenti autorizzati separati da una virgola
 *        (es. 123456789,987654321)
 *    è anche possibile cambiare la password di configurazione
 *    e salvare.
 * 
 * 3. Dopo la configurazione, il sistema si riavvia e si connette
 *    automaticamente ai servizi configurati.
 * 
 * 4. Premendo il tasto RESET per tre secondi si rientra in modalità
 *    di configurazione. Da qui, oltre che modificare utenti, SSID e
 *    BOT_TOKEN, si può resettare e cancellare tutti i dati tornando
 *    alla configurazione iniziale
 *
 * 5. Utilizza un display Oled da 0.92 inch SSD1306
 *    di 128x64 pixel.
 * 
 * COLLEGAMENTI HARDWARE (per D1 mini):
 * - D5 (GPIO14)  -> Relè Porta 
 * - D8 (GPIO15)  -> Relè Luce 
 * - D4 (GPIO2)   -> LED Status (LED blu integrato sul D1 mini)
 * - D7 (GPIO13)  -> Buzzer (opzionale)
 * - D1 (GPIO5)   -> SCL OLED Display
 * - D2 (GPIO4)   -> SDA OLED Display
 * - D3 (GPIO0)   -> Pulsante "Porta"(NO=Normalmente Aperto) tra GPIO0 e massa
 * - D0 (GPIO16)  -> Pulsante "RESET"(NO=Normalmente Aperto) tra GPIO16 e massa
 * - 3.3V/5V -> VCC dei relè
 * - 3.3V Vcc OLED Display
 * - GND     -> GND comune/GND OLED Display
 *
 * per una descrizione più dettagliata:
 * https://github.com/FyaSKoBoVe/apriporta_telegram.git
 *
 * Autore: FyaSKoBoVe
 *
 * Anno: 2025
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
 * @brief Gestore dell'interrupt del pulsante fisico apriporta.
 *        Attiva il flag solo dopo un debounce per evitare falsi trigger.
 * 
 * Il debounce serve per evitare che il pulsante venga rilevato più volte
 * in caso di bounce (rimbalzo) del pulsante.
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
 * @param textSize Dimensione del testo (1x, 2x...) (default = 1)
 * 
 * @return void
 */
void drawCenteredText(String text, int y, int textSize = 1) {
  /**
   * @brief Recupera le dimensioni del testo
   * 
   * @param text Testo da misurare
   * @param x1 Indirizzo X dell'angolo superiore sinistro
   * @param y1 Indirizzo Y dell'angolo superiore sinistro
   * @param w Larghezza del testo
   * @param h Altezza del testo
   * 
   * @return void
   */
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
 * 
 * @param ago Tempo trascorso in millisecondi
 * 
 * @return Stringa formattata con il tempo trascorso
 */
String formatTimeAgoShort(unsigned long ago) {
  unsigned long seconds = ago / 1000;
  // Se il tempo trascorso è inferiore a 1 minuto, mostralo in secondi
  if (seconds < 60) return String(seconds) + "s ";
  // Se il tempo trascorso è inferiore a 1 ora, mostralo in minuti
  unsigned long minutes = seconds / 60;
  if (minutes < 60) return String(minutes) + "m ";
  // Se il tempo trascorso è inferiore a 1 giorno, mostralo in ore
  unsigned long hours = minutes / 60;
  if (hours < 24) return String(hours) + "h ";
  // Se il tempo trascorso è superiore a 1 giorno, mostralo in giorni
  unsigned long days = hours / 24;
  return String(days) + "g ";
}

/**
 * @brief Restituisce una riga formattata per il log sul display OLED.
 *        Mostra nome utente, tipo di operazione e quando è stata eseguita.
 * 
 * @param idx Indice dell'operazione nel log
 * 
 * @return Stringa formattata con il log
 */
String getShortLogLine(int idx) {
  if (operationLog[idx].timestamp == 0) return "";
  // Recupera l'azione corrispondente all'operazione
  String action;
  if (operationLog[idx].operation == "LUCE_ACCESA") {
    action = "Luce";
  } else if (operationLog[idx].operation == "PORTA_PULSANTE") {
    action = "Porta";
  } else {
    action = "Porta";
  }
  // Recupera il nome utente dell'operazione
  String name = operationLog[idx].userName;
  // Recupera il tempo trascorso dall'operazione
  String ago = formatTimeAgoShort(millis() - operationLog[idx].timestamp);
  // Formatta la riga per il log
  return name + " " + action + " " + ago;
}

/**
 * @brief Restituisce lo stato della connessione WiFi e Telegram per il display.
 * 
 * @return Stringa formattata con lo stato delle connessioni
 */
String getConnStatusLine() {
  // Prepara le stringhe per lo stato delle connessioni
  String w = wifiOk ? "WiFi:OK" : "WiFi:--";
  String t = telegramOk ? "TG:OK" : "TG:--";
  // Restituisce la stringa con lo stato delle connessioni
  return w + " " + t;
}

/**
 * @brief Aggiorna il display OLED con informazioni correnti:
 *        - Titolo
 *        - Ultime operazioni
 *        - Stato connessioni
 */
void updateDisplay() {
  // Prepara le righe per il display OLED
  String line1 = "D1 Mini Apriporta";
  String line2 = getShortLogLine(0); // Ultima operazione
  String line3 = getShortLogLine(1); // Seconda operazione
  String line4 = getShortLogLine(2); // Terza operazione
  String line5 = getShortLogLine(3); // Quarta operazione
  String line6 = getConnStatusLine(); // Stato delle connessioni

  // Concatena le righe in una stringa
  String displayContent = line1 + "|" + line2 + "|" + line3 + "|" + line4 + "|" + line5 + "|" + line6;
  
  // Evita aggiornamenti inutili
  if (displayContent == lastDisplayContent) return;
  lastDisplayContent = displayContent;

  // Pulisce e reimposta il display
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Stampa le righe
  drawCenteredText("D1 Mini Apriporta", 0, 1); // Titolo
  display.setCursor(0, 16);
  display.println(line2); // Ultima operazione
  display.setCursor(0, 26); 
  display.println(line3); // Seconda operazione
  display.setCursor(0, 36); 
  display.println(line4); // Terza operazione
  display.setCursor(0, 46); 
  display.println(line5); // Quarta operazione
  drawCenteredText(getConnStatusLine(), 57); // Stato delle connessioni
  display.display();
}

// ==================== EEPROM CONFIG ======================
/**
 * @brief Carica la configurazione salvata in EEPROM.
 *        Se non configurato, azzera tutto e inizializza
 *       la password di configurazione con valore predefinito.
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

// ============ SALVA CONFIGURAZIONE IN EEPROM ============
/**
 * @brief Salva la configurazione corrente in EEPROM.
 *        Utilizza la funzione `put()` per scrivere i dati
 *        e `commit()` per assicurarne la persistenza.
 *        Termina la modalità di scrittura con `end()`.
 */
void saveConfig() {
  EEPROM.begin(EEPROM_SIZE); // Inizializza la memoria EEPROM
  EEPROM.put(0, userConfig); // Memorizza la configurazione a partire dall'indirizzo 0
  EEPROM.commit();           // Assicura che i dati siano scritti sulla memoria flash
  EEPROM.end();              // Termina la modalità di accesso alla EEPROM
}

// ===== PARSING DELLA STRINGA UTENTI IN ARRAY DI LONG =====
/**
 * @brief Converte la stringa degli utenti autorizzati in array di chat ID.
 *        Ogni chat ID è separato da virgola.
 * 
 * @note La funzione utilizza la funzione strtok() per dividere la stringa in
 *       token e ripetere l'operazione fino a quando non si raggiunge la fine
 *       della stringa o il numero massimo di utenti autorizzati.
 * 
 * @note La funzione stampa un messaggio di debug per ogni utente autorizzato
 *       caricato e il numero totale di utenti autorizzati.
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
 *
 * @note La funzione utilizza la funzione `write()` per scrivere zero in ogni cella
 *       della EEPROM e la funzione `commit()` per salvare i dati sulla flash.
 *       La funzione `end()` disattiva la modalità di scrittura sulla EEPROM.
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
 *        La funzione resetta completamente la EEPROM azzerando tutti i dati salvati.
 *        Utile in caso di errore o per ripristinare le impostazioni predefinite.
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
  // Verifica se è presente l'argomento "cfgpass" e se corrisponde alla password di configurazione
  if (server.hasArg("cfgpass") && strcmp(server.arg("cfgpass").c_str(), userConfig.configPassword) == 0) {
    // Se la password è corretta, imposta la variabile passwordOk a true
    passwordOk = true;
    // Reindirizza alla pagina principale con uno status code 302
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  } else {
    // Se la password è errata, restituisce un errore 403 con un messaggio di errore
    server.send(403, "text/html", "<h1>Password configurazione errata!</h1>");
  }
}

/**
 * @brief Gestore della richiesta POST per salvare i nuovi parametri di configurazione.
 *        Aggiorna SSID, password WiFi, token bot e utenti autorizzati.
 *        Verifica l'autorizzazione prima di procedere.
 */
void handleSave() {
  // Controlla se l'utente è autorizzato
  if (!passwordOk) {
    server.send(403, "text/html", "<h1>Non autorizzato</h1>");
    return;
  }

  // Verifica la presenza di tutti i parametri richiesti
  if (server.hasArg("ssid") && server.hasArg("pass") && server.hasArg("token") && server.hasArg("users")) {
    // Recupera i nuovi valori dai parametri POST
    String newSsid = server.arg("ssid");
    String newPass = server.arg("pass");
    String newToken = server.arg("token");
    String newUsers = server.arg("users");

    // Azzera i vecchi valori nella configurazione
    memset(userConfig.ssid, 0, sizeof(userConfig.ssid));
    memset(userConfig.password, 0, sizeof(userConfig.password));
    memset(userConfig.botToken, 0, sizeof(userConfig.botToken));
    memset(userConfig.authorizedUsers, 0, sizeof(userConfig.authorizedUsers));

    // Copia i nuovi valori nella configurazione
    strncpy(userConfig.ssid, newSsid.c_str(), sizeof(userConfig.ssid) - 1);
    strncpy(userConfig.password, newPass.c_str(), sizeof(userConfig.password) - 1);
    strncpy(userConfig.botToken, newToken.c_str(), sizeof(userConfig.botToken) - 1);
    strncpy(userConfig.authorizedUsers, newUsers.c_str(), sizeof(userConfig.authorizedUsers) - 1);

    // Segna la configurazione come completata e salva in EEPROM
    userConfig.configured = true;
    saveConfig();

    // Invia una risposta di successo e riavvia il dispositivo
    server.send(200, "text/html", "<h1>Configurazione Salvata!</h1><p>Il dispositivo si riavvia...</p>");
    delay(2000);
    ESP.restart();
  } else {
    // Risponde con un errore se mancano parametri
    server.send(400, "text/plain", "Parametri mancanti.");
  }
}

/**
 * @brief Gestore per le richieste HTTP non trovate.
 *        Reindirizza sempre alla root `/`.
 *
 * La funzione gestisce le richieste HTTP non valide o non trovate e
 * reindirizza l'utente alla root `/`, che e' la pagina di login.
 */
void handleNotFound() {
  // Invia un header "Location" con la root `/` e un codice di stato 302
  server.sendHeader("Location", "/", true);
  // Invia una risposta con codice di stato 302 e contenuto vuoto
  server.send(302, "text/plain", "");
}

// ======= PAGINA CAMBIO PASSWORD CONFIGURAZIONE =======
/**
 * @brief Gestisce la visualizzazione della pagina HTML per cambiare
 *        la password di accesso alla configurazione. Include uno script
 *        per la verifica della corrispondenza delle nuove password.
 */
void handleChangePassword() {
  // HTML della pagina di cambio password
  String html = R"rawliteral(
    <!DOCTYPE HTML>
    <html>
    <head>
      <title>Cambia Password</title>
      <meta name="viewport" content="width=device-width, initial-scale=1">
      <style>
        /* Stile per bottoni e input */
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
        /* Stile per bottoni attivi */
        button:active, input[type=submit]:active {
          background-color: #1769aa;
        }
        /* Stile per link a forma di bottone */
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
        /* Stile per link attivi */
        a.button-link:active {
          background-color: #1769aa;
        }
      </style>
    </head>
    <body>
      <h1>Cambia Password Configurazione</h1>
      <!-- Form per il cambio della password -->
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
      <!-- Link per tornare alla pagina di configurazione -->
      <a href="/" class="button-link">Torna alla configurazione</a>
      <script>
        /**
         * @brief Controlla che le due nuove password inserite coincidano.
         * @return true se coincidono, altrimenti false e mostra un alert.
         */
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

  // Invia la risposta con il contenuto HTML
  server.send(200, "text/html", html);
}

/**
 * @brief Gestore POST per modificare la password di configurazione.
 *        Verifica che la password attuale sia corretta e che le due nuove
 *        password coincidano. Salva la nuova password in EEPROM e
 *        reindirizza all'utente alla pagina di configurazione.
 */
void handleChangePasswordPost() {
  if (!server.hasArg("oldpw") || !server.hasArg("newpw") || !server.hasArg("newpw2")) {
    // Risponde con un errore 400 se mancano parametri
    server.send(400, "text/plain", "Parametri mancanti.");
    return;
  }

  // Recupera i parametri POST
  String oldpw = server.arg("oldpw");
  String newpw = server.arg("newpw");
  String newpw2 = server.arg("newpw2");

  if (strcmp(oldpw.c_str(), userConfig.configPassword) != 0) {
    // Risponde con un errore 403 se la password attuale e' errata
    server.send(403, "text/html", "<h1>Password attuale errata!</h1>");
    return;
  }

  if (newpw != newpw2) {
    // Risponde con un errore 400 se le nuove password non coincidono
    server.send(400, "text/html", "<h1>Le nuove password non coincidono!</h1><a href=\"/changepw\" class=\"button-link\">Riprova</a>");
    return;
  }

  // Salva la nuova password in EEPROM
  strncpy(userConfig.configPassword, newpw.c_str(), MAX_PASSCFG_LEN-1);
  saveConfig();

  // Risponde con un successo e reindirizza l'utente
  server.send(200, "text/html", "<h1>Password aggiornata!</h1><a href=\"/\" class=\"button-link\">Torna alla configurazione</a>");
}

// ========== SETUP ACCESS POINT E SERVER ==========
/**
 * @brief Configura l'ESP8266 in modalita' Access Point (AP).
 *        Avvia il server web e il DNS per la configurazione iniziale.
 *        Questa funzione e' chiamata solo se l'ESP8266 non riesce a connettersi
 *        a una rete WiFi.
 */
void setupAP() {
  passwordOk = false; // Richiedi sempre la password all'ingresso in config!
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP_Config", "");
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  // Registra le funzioni di gestione delle richieste
  server.on("/", handleRoot);
  server.on("/login", HTTP_POST, handleLogin);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/changepw", HTTP_GET, handleChangePassword);
  server.on("/changepw", HTTP_POST, handleChangePasswordPost);
  server.on("/reset_eeprom", HTTP_POST, handleResetEEPROM); // <-- handler reset
  server.onNotFound(handleNotFound);

  server.begin();

  // Mostra un messaggio sul display OLED per informare l'utente
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
 * @brief Tenta di connettersi alla rete WiFi configurata
 *        per un massimo di 30 secondi (30 tentativi da 1s).
 *        Aggiorna lo stato WiFi e il display.
 * @return true se la connessione WiFi va a buon fine,
 *         false altrimenti.
 */
bool connectToWiFiAndCheck() {
  // Se non configurato, non posso connettermi
  if (!userConfig.configured) return false;

  // Imposta ESP8266 in modalità Station
  WiFi.mode(WIFI_STA);
  // Avvia connessione alla rete configurata
  WiFi.begin(userConfig.ssid, userConfig.password);

  int attempts = 0;
  // Prova a connetterti alla rete per un massimo di 30 secondi
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(1000); // Aspetta 1 secondo
    attempts++;  // Incrementa contatore tentativi
  }

  // Aggiorna lo stato della connessione WiFi
  wifiOk = (WiFi.status() == WL_CONNECTED);
  // Aggiorna il display OLED
  updateDisplay();

  if (wifiOk) {
    Serial.println("\nWiFi connesso!");
    Serial.print("Indirizzo IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Intensità segnale: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    // Segnale acustico di successo
    successBeep();
  } else {
    Serial.println("\nErrore connessione WiFi!");
    // Segnale acustico di errore
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
  Serial.begin(115200); // Velocita'  di comunicazione seriale
  //delay(1500);

  // ====== BENVENUTO SU SERIALE ======
  Serial.println("\n\n=================================");
  Serial.println("=== D1 mini Telegram Citofono ===");
  Serial.println("=================================\n");
  Serial.println("Inizializzazione sistema...\n");

  // ====== INIZIALIZZAZIONE DISPLAY OLED ======
  /**
   * @brief Inizializza il display OLED SSD1306.
   *        Verifica la presenza del display e lo pulisce.
   */
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // 0x3C  l'indirizzo I2C del display
    Serial.println(F("SSD1306 non trovato!")); // Errore se il display non risponde
  }
  display.clearDisplay(); // Pulisce il display
  display.display(); // Mostra lo schermo 

  // ====== CARICA CONFIGURAZIONE DALLA EEPROM ======
  /**
   * @brief Carica la configurazione da EEPROM.
   *        Carica le informazioni di configurazione
   *        da EEPROM.
   */
  loadConfig(); // Carica SSID, password, token bot, ecc.

  // ====== PARSE UTENTI AUTORIZZATI ======
  /**
   * @brief Converte la stringa in array di chat ID.
   *        Parsa la stringa contenente gli utenti autorizzati
   *        e li aggiunge all'array authorizedUsers.
   */
  parseAuthorizedUsers(); // Converte la stringa in array di chat ID

  // ====== CONFIGURAZIONE DEI PIN ======
  pinMode(RELAY_PORTA, OUTPUT); // Rele'  per aprire la porta
  pinMode(RELAY_LUCE, OUTPUT);  // Rele'  per accendere la luce
  pinMode(LED_STATUS, OUTPUT); // LED blu per stato
  pinMode(BUZZER_PIN, OUTPUT); // Buzzer per feedback sonoro
  pinMode(BUTTON_PORTA, INPUT_PULLUP); // Pulsante fisico apriporta con pull-up
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP); // Pulsante reset configurazione

  // ====== ATTACH INTERRUPT PER IL PULSANTE ======
  attachInterrupt(digitalPinToInterrupt(BUTTON_PORTA), buttonISR, FALLING); // Attiva interrupt su fronte discendente
  
  // ====== RESET DEI RELE' E OUTPUT ======
  digitalWrite(RELAY_PORTA, LOW);  // Assicura che la porta sia chiusa
  digitalWrite(RELAY_LUCE, LOW);   // Assicura che la luce sia spenta
  digitalWrite(LED_STATUS, LOW);  // Spegni LED status
  digitalWrite(BUZZER_PIN, LOW);  // Spegni buzzer

  // ====== CONNESSIONE A WIFI ======
  if (!userConfig.configured || !connectToWiFiAndCheck()) {
    // Se non configurato o errore WiFi, entra in modalita' configurazione
    setupAP();
    while (true) {
      dnsServer.processNextRequest(); // Gestore DNS
      server.handleClient(); // Gestore HTTP
      delay(10); // Piccola pausa per non sovraccaricare il processore
    }
  }

  // ====== CONFIGURA IL BOT TELEGRAM ======
  bot = UniversalTelegramBot(userConfig.botToken, client); // Imposta il token del bot
  client.setInsecure(); // Disabilita controllo certificati per compatibilita' 

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
  // Controlla se il pulsante di reset è premuto
  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    // Se il pulsante è appena stato premuto, avvia il timer
    if (resetButtonPressTime == 0) {
      resetButtonPressTime = millis();
    } 
    // Se il pulsante è tenuto premuto per più di 3 secondi, entra in modalità configurazione
    else if (millis() - resetButtonPressTime > 3000 && !configModeRequested) {
      configModeRequested = true;

      // Segnale acustico di conferma dell'ingresso in modalità configurazione
      tone(BUZZER_PIN, 1000, 500);
      delay(600);
      tone(BUZZER_PIN, 1500, 500);
      delay(600);
      noTone(BUZZER_PIN);

      // Avvia modalità configurazione
      setupAP();
      while (true) {
        dnsServer.processNextRequest(); // Gestisce le richieste DNS
        server.handleClient();          // Gestisce il server web
        delay(10);                      // Piccola pausa per non sovraccaricare la CPU
      }
    }
  } else {
    // Resetta lo stato del pulsante quando viene rilasciato
    resetButtonPressTime = 0;
    configModeRequested = false;
  }

  // ====== POLLING MESSAGGI TELEGRAM ======
  // Controlla i nuovi messaggi Telegram periodicamente
  if (millis() > lastTimeBotRan + BOT_REQUEST_DELAY) {
    checkTelegramMessages();
    lastTimeBotRan = millis();
  }

  // ====== GESTIONE TIMEOUT AUTOMATICI ======
  handleTimeouts(); // Gestisce la chiusura automatica della porta e lo spegnimento delle luci

  // ====== LAMPEGGIO LED DI STATO ======
  blinkStatusLED(); // Fa lampeggiare il LED di stato

  // ====== CONTROLLO CONNESSIONE WIFI ======
  checkWiFiConnection(); // Verifica periodicamente la connessione WiFi

  // ====== CONTROLLO PULSANTE FISICO APRIPORTA ======
  checkButtonApriPorta(); // Controlla se è stato premuto il pulsante fisico per aprire la porta

  // ====== AGGIORNAMENTO DISPLAY OLED ======
  updateDisplay(); // Aggiorna il contenuto mostrato sul display OLED
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
 * 
 * Questa funzione viene chiamata una volta sola all'avvio del sistema per verificare
 * se il token del bot Telegram è valido e se la connessione va a buon fine.
 * Il risultato viene visualizzato sul display OLED e sul seriale.
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
 * 
 * Questa funzione viene chiamata una volta sola all'avvio del sistema
 * per mostrare un messaggio di benvenuto al seriale e sul display OLED.
 * Inoltre, riproduce una melodia iniziale utilizzando il buzzer.
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

  digitalWrite(LED_STATUS, HIGH); // Accende LED status
  confirmationBeep(); // Riproduce melodia iniziale
}

// =============== GESTIONE MESSAGGI TELEGRAM ==============
/**
 * @brief Controlla se ci sono nuovi messaggi Telegram.
 *        Invia una richiesta al bot e aggiorna lo stato di connessione.
 *
 * Questa funzione viene chiamata periodicamente per controllare se sono
 * arrivati nuovi messaggi Telegram. Se ci sono nuovi messaggi, vengono
 * processati chiamando la funzione `handleNewMessages()`.
 */
void checkTelegramMessages() {
  // Ottiene il numero di nuovi messaggi ricevuti
  int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

  // Aggiorna lo stato di connessione
  telegramOk = (numNewMessages >= 0);

  // Aggiorna il display OLED
  updateDisplay();

  // Processa i nuovi messaggi
  while (numNewMessages) {
    handleNewMessages(numNewMessages);

    // Ottiene il numero di nuovi messaggi ricevuti
    numNewMessages = bot.getUpdates(bot.last_message_received + 1);

    // Aggiorna lo stato di connessione
    telegramOk = (numNewMessages >= 0);

    // Aggiorna il display OLED
    updateDisplay();
  }
}

/**
 * @brief Gestisce i nuovi messaggi ricevuti dal bot Telegram.
 *        Verifica l'autorizzazione dell'utente e chiama il processore di comandi.
 *
 * @param numNewMessages Numero di nuovi messaggi ricevuti.
 */
void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    // Controlla il tipo di messaggio ricevuto
    if (bot.messages[i].type == "message") {
      String chat_id = bot.messages[i].chat_id;
      String text = bot.messages[i].text;
      String from_name = bot.messages[i].from_name;
      long user_id = chat_id.toInt();

      // Verifica se l'utente è autorizzato
      if (!isAuthorizedUser(user_id)) {
        handleUnauthorizedUser(chat_id, from_name);
        continue;
      }

      // Processa il comando testuale
      processCommand(chat_id, text, from_name, user_id);
    } 
    else if (bot.messages[i].type == "callback_query") {
      String chat_id = bot.messages[i].chat_id;
      String callback_data = bot.messages[i].text;
      String from_name = bot.messages[i].from_name;
      String callback_query_id = bot.messages[i].query_id;
      long user_id = chat_id.toInt();

      // Verifica se l'utente è autorizzato
      if (!isAuthorizedUser(user_id)) {
        bot.answerCallbackQuery(callback_query_id, "❌ Non autorizzato");
        continue;
      }

      // Processa il callback della query
      processCallback(chat_id, callback_data, from_name, user_id, callback_query_id);
    }
  }
}

/**
 * @brief Processa un comando testuale ricevuto da Telegram.
 * 
 * @param chat_id ID della chat Telegram
 * @param text Comando testuale ricevuto
 * @param from_name Nome dell'utente che ha inviato il comando
 * @param user_id ID dell'utente che ha inviato il comando
 */
void processCommand(String chat_id, String text, String from_name, long user_id) {
  // Controlla il comando ricevuto e chiama la funzione appropriata
  if (text == "/start") {
    // Invia un messaggio di benvenuto
    sendWelcomeMessage(chat_id);
  }
  else if (text == "/apri") {
    // Processa il comando per aprire la porta
    handleApriPorta(chat_id, from_name, user_id);
  }
  else if (text == "/luce") {
    // Processa il comando per accendere la luce
    handleAccendiLuce(chat_id, from_name, user_id);
  }
  else if (text == "/stato") {
    // Mostra lo stato del sistema
    handleStatoSistema(chat_id);
  }
  else if (text == "/log") {
    // Mostra il registro delle operazioni
    handleMostraLog(chat_id);
  }
  else if (text == "/help") {
    // Invia un messaggio di aiuto con i comandi disponibili
    sendHelpMessage(chat_id);
  }
  else if (text == "/menu") {
    // Invia il menu principale con i pulsanti interattivi
    sendMainMenu(chat_id);
  }
  else {
    // Gestisce il comando non riconosciuto
    handleUnknownCommand(chat_id);
  }
}

/**
 * @brief Processa un comando inviato tramite pulsanti inline Telegram.
 *        Chiama la funzione appropriata in base al tipo di comando ricevuto.
 *
 * @param chat_id ID della chat Telegram
 * @param callback_data Tipo di comando ricevuto
 * @param from_name Nome dell'utente che ha inviato il comando
 * @param user_id ID dell'utente che ha inviato il comando
 * @param query_id ID della query Telegram
 */
void processCallback(String chat_id, String callback_data, String from_name, long user_id, String query_id) {
  // Controlla il tipo di comando ricevuto e chiama la funzione appropriata
  if (callback_data == "APRI_PORTA") {
    handleApriPortaCallback(chat_id, from_name, user_id, query_id);
    // Apri la porta e registra l'operazione
  }
  else if (callback_data == "ACCENDI_LUCE") {
    handleAccendiLuceCallback(chat_id, from_name, user_id, query_id);
    // Accendi la luce e registra l'operazione
  }
  else if (callback_data == "STATO_SISTEMA") {
    handleStatoSistemaCallback(chat_id, query_id);
    // Mostra lo stato del sistema
  }
  else if (callback_data == "MOSTRA_LOG") {
    handleMostraLogCallback(chat_id, query_id);
    // Mostra il registro delle operazioni
  }
  else if (callback_data == "AIUTO") {
    handleAiutoCallback(chat_id, query_id);
    // Mostra la guida completa
  }
  else {
    bot.answerCallbackQuery(query_id, "❌ Comando non riconosciuto");
    // Gestisce il comando non riconosciuto
  }
}

// ===================== GESTIONE COMANDI ==================
/**
 * @brief Invia un messaggio di benvenuto all'utente che usa `/start`.
 *        Il messaggio include un elenco dei comandi disponibili e un link al menu
 *        con i pulsanti interattivi.
 * @param chat_id ID della chat Telegram
 */
void sendWelcomeMessage(String chat_id) {
  // Messaggio di benvenuto
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

  // Invia il messaggio di benvenuto
  bot.sendMessage(chat_id, welcome, "Markdown");

  // Aggiorna il display
  updateDisplay();

  // Invia il menu principale
  delay(500);
  sendMainMenu(chat_id);
}

// =================== GESTIONE APRIPORTA ==================
/**
 * @brief Apri la porta e registra l'operazione.
 *        La funzione apre la porta, registra l'operazione e invia un messaggio
 *        di conferma all'utente.
 * @param chat_id ID della chat Telegram
 * @param from_name Nome dell'utente che ha inviato il comando
 * @param user_id ID dell'utente che ha inviato il comando
 */
void handleApriPorta(String chat_id, String from_name, long user_id) {
  apriPorta();

  // Creazione del messaggio di risposta
  String message = "🚪 *Porta Aperta*\n\n";
  message += "✅ Comando eseguito con successo\n";
  message += "⏰ La porta è stata APERTA\n";
  message += "👤 Richiesta da: " + from_name;

  // Invio del messaggio di risposta
  bot.sendMessage(chat_id, message, "Markdown");

  // Registrazione dell'operazione
  logOperation(user_id, "PORTA_APERTA", from_name);

  // Aggiornamento del display
  updateDisplay();

  // Debugging
  Serial.println("✅ Porta aperta su richiesta di: " + from_name);

  // Beep di conferma
  confirmationBeep();

  // Ritardo di 2 secondi prima di inviare il menu
  delay(2000);

  // Invio del menu principale
  sendMainMenu(chat_id);
}

// ================= GESTIONE ACCENDI LUCE =================
/**
 * @brief Accendi la luce delle scale e registra l'operazione.
 *        Invia un messaggio di conferma all'utente e aggiorna il display.
 *
 * @param chat_id ID della chat Telegram
 * @param from_name Nome dell'utente che ha inviato il comando
 * @param user_id ID dell'utente che ha inviato il comando
 */
void handleAccendiLuce(String chat_id, String from_name, long user_id) {
  // Accende la luce delle scale
  accendiLuce();

  // Crea il messaggio di conferma
  String message = "💡 *Luce Accesa*\n\n";
  message += "✅ Luce delle scale accesa\n";
  message += "⏰ Si spegnerà automaticamente tra 30 secondi\n";
  message += "👤 Richiesta da: " + from_name;

  // Invia il messaggio di conferma tramite Telegram
  bot.sendMessage(chat_id, message, "Markdown");

  // Registra l'operazione nel log
  logOperation(user_id, "LUCE_ACCESA", from_name);

  // Aggiorna il display con lo stato corrente
  updateDisplay();

  // Stampa sul seriale per debugging
  Serial.println("✅ Luce accesa su richiesta di: " + from_name);

  // Riproduce un beep di conferma
  confirmationBeep();

  // Attende 2 secondi prima di inviare il menu principale
  delay(2000);
  
  // Invia il menu principale con i pulsanti interattivi
  sendMainMenu(chat_id);
}

// =========== GESTIONE PULSANTE FISICO APRIPORTA ==========
/**
 * @brief Controlla se è stato premuto il pulsante fisico per aprire la porta.
 *
 * Questa funzione viene chiamata periodicamente per verificare se il pulsante
 * fisico per aprire la porta è stato premuto. Se sì, viene chiamata la funzione
 * apriPortaDaPulsante() per aprire la porta e registrare l'evento.
 */
void checkButtonApriPorta() {
  if (buttonInterruptFlag) {
    buttonInterruptFlag = false;
    apriPortaDaPulsante(); // Apre la porta e registra l'evento
  }
}

// ================= APRIPORTA DA PULSANTE =================
/**
 * @brief Aziona la porta tramite il pulsante fisico e registra l'evento.
 *        Questa funzione viene chiamata quando il pulsante fisico per aprire la porta
 *        viene premuto. Apre la porta e registra l'operazione nel log.
 */
void apriPortaDaPulsante() {
  // Apre la porta e registra l'operazione
  apriPorta();
  logOperation(0, "PORTA_PULSANTE", "Pulsante");

  // Stampa sul seriale per debugging
  Serial.println("✅ Porta aperta da pulsante fisico");

  // Riproduce un beep di conferma
  confirmationBeep();

  // Aggiorna il display con lo stato corrente
  updateDisplay();
}

// ================== GESTIONE STATO SISTEMA ==================
/**
 * @brief Gestisce il comando `/stato` inviando tramite Telegram lo stato completo del sistema.
 *        Utilizza la funzione `getDetailedSystemStatus()` per recuperare i dati di stato formattati.
 * 
 * @param chat_id ID della chat Telegram a cui inviare lo stato del sistema
 */
void handleStatoSistema(String chat_id) {
  // Ottiene lo stato dettagliato del sistema
  String status = getDetailedSystemStatus();

  // Invia lo stato del sistema al chat_id specificato in formato Markdown
  bot.sendMessage(chat_id, status, "Markdown");
  
  // Attende 1 secondo prima di inviare il menu principale
  delay(1000);
  
  // Invia il menu principale con i pulsanti interattivi
  sendMainMenu(chat_id);
}

// ====================== MOSTRA LOG =======================
/**
 * @brief Gestisce il comando `/log` inviando tramite Telegram il registro delle ultime operazioni.
 *        Richiama la funzione `getFormattedLog()` per ottenere i dati formattati.
 * 
 * @param chat_id ID della chat Telegram a cui inviare il registro delle operazioni
 */
void handleMostraLog(String chat_id) {
  // Ottiene il registro delle operazioni formattato
  String logStr = getFormattedLog();
  
  // Invia il registro delle operazioni al chat_id specificato in formato Markdown
  bot.sendMessage(chat_id, logStr, "Markdown");
  
  // Attende 1 secondo prima di inviare il menu principale
  delay(1000);
  
  // Invia il menu principale con i pulsanti interattivi
  sendMainMenu(chat_id);
}

// =================== MESSAGGIO DI HELP ===================
/**
 * @brief Gestisce il comando `/help` mostrando una guida completa dei comandi disponibili.
 *        Include informazioni sui comandi principali, informativi, sulla sicurezza e sull'uso del sistema.
 * 
 * @param chat_id ID della chat Telegram a cui inviare la guida
 */
void sendHelpMessage(String chat_id) {
  // Messaggio di aiuto con i comandi principali e informativi
  String help = "❓ *Guida Comandi Sistema Citofono*\n\n";
  help += "*Comandi Principali:*\n";
  help += "🚪 `/apri` - Apre la porta d'ingresso\n";
  help += "💡 `/luce` - Accende luce scale\n";
  help += "*Comandi Informativi:*\n";
  help += "ℹ️ `/stato` - Stato completo del sistema\n";
  help += "📋 `/log` - Ultimi log operazioni\n";
  help += "❓ `/help` - Questa guida\n\n";
  // Informazioni sulla sicurezza del sistema
  help += "*Sicurezza:*\n";
  help += "🔒 Solo utenti autorizzati possono usare il sistema\n";
  help += "📊 Tutte le operazioni vengono registrate\n";
  help += "🔄 Timeout dei Rele' per sicurezza\n";

  // Invia la guida tramite Telegram in formato Markdown
  bot.sendMessage(chat_id, help, "Markdown");

  // Attende 1 secondo prima di inviare il menu principale
  delay(1000);

  // Invia il menu principale con i pulsanti interattivi
  sendMainMenu(chat_id);
}

// ================== COMANDO SCONOSCIUTO ==================
/**
 * @brief Risponde a un comando non riconosciuto fornendo indicazioni su come procedere.
 *        Suggerisce l'uso di `/help` o `/menu`.
 *
 * @param chat_id ID della chat Telegram a cui rispondere
 */
void handleUnknownCommand(String chat_id) {
  String message = "❓ *Comando Non Riconosciuto*\n\n";
  message += "Il comando inserito non è valido.\n";
  message += "Usa /help per vedere tutti i comandi disponibili o /menu per i pulsanti.";
  // Invia la risposta tramite Telegram in formato Markdown
  bot.sendMessage(chat_id, message, "Markdown");
}

// ================ CONTROLLO AUTORIZZAZIONI ===============
/**
 * @brief Gestisce gli accessi non autorizzati al sistema.
 *        Invia un messaggio di errore e un segnale acustico.
 * @param chat_id ID della chat Telegram a cui rispondere
 * @param from_name Nome dell'utente che ha tentato l'accesso
 */
void handleUnauthorizedUser(String chat_id, String from_name) {
  String message = "🚫 *Accesso Negato*\n\n";
  message += "Non sei autorizzato ad utilizzare questo sistema.\n";
  message += "Contatta l'amministratore per richiedere l'accesso.";
  // Invia la risposta tramite Telegram in formato Markdown
  bot.sendMessage(chat_id, message, "Markdown");
  // Stampa sul seriale per debugging
  Serial.println("🚫 Accesso negato per: " + from_name + " (ID: " + chat_id + ")");
  // Segnale acustico di errore
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
 *
 * @param chat_id ID della chat Telegram a cui inviare il menu
 */
void sendMainMenu(String chat_id) {
  // Testo del menu principale
  String menuText = "🎛️ *Menu Controllo Citofono*\n\n";
  menuText += "Scegli un'azione usando i pulsanti qui sotto:";

  // Definizione della tastiera interattiva con i pulsanti
  String keyboard = "[[{\"text\":\"🚪 Apri Porta\",\"callback_data\":\"APRI_PORTA\"}],"
                    "[{\"text\":\"💡 Accendi Luce\",\"callback_data\":\"ACCENDI_LUCE\"}],"
                    "[{\"text\":\"ℹ️ Stato Sistema\",\"callback_data\":\"STATO_SISTEMA\"},"
                    "{\"text\":\"📋 Mostra Log\",\"callback_data\":\"MOSTRA_LOG\"}],"
                    "[{\"text\":\"❓ Aiuto\",\"callback_data\":\"AIUTO\"}]]";

  // Invia il menu con tastiera interattiva al chat_id specificato
  bot.sendMessageWithInlineKeyboard(chat_id, menuText, "Markdown", keyboard);
}

/**
 * @brief Gestisce il callback del pulsante "Apri Porta" nel menu Telegram.
 *        La funzione apre la porta e registra l'operazione nel log.
 * @param chat_id ID della chat Telegram a cui rispondere
 * @param from_name Nome dell'utente che ha inviato il comando
 * @param user_id ID dell'utente che ha inviato il comando
 * @param query_id ID della query Telegram
 */
void handleApriPortaCallback(String chat_id, String from_name, long user_id, String query_id) {
  // Apri la porta
  apriPorta();

  // Rispondi al callback con un messaggio di conferma
  bot.answerCallbackQuery(query_id, "🚪 Porta Aperta! Apriporta Azionato!");

  // Crea un messaggio di risposta con informazioni sull'operazione
  String message = "🚪 *Porta Aperta*\n\n";
  message += "✅ Comando eseguito con successo\n";
  message += "⏰ La porta è stata aperta!\n";
  message += "👤 Richiesta da: " + from_name;

  // Invia il messaggio di risposta al chat_id specificato
  bot.sendMessage(chat_id, message, "Markdown");

  // Registra l'operazione nel log
  logOperation(user_id, "PORTA_APERTA", from_name);

  // Aggiorna il display
  updateDisplay();

  // Stampa sul seriale per debugging
  Serial.println("✅ Porta aperta su richiesta di: " + from_name);

  // Segnale acustico di conferma
  confirmationBeep();

  // Invia il menu principale
  sendMainMenu(chat_id);
}

/**
 * @brief Gestisce il callback del pulsante "Accendi Luce" nel menu Telegram.
 *        Accende la luce, registra l'operazione nel log e invia un messaggio di conferma.
 * 
 * @param chat_id ID della chat Telegram
 * @param from_name Nome dell'utente che ha inviato il comando
 * @param user_id ID dell'utente che ha inviato il comando
 * @param query_id ID della query Telegram
 */
void handleAccendiLuceCallback(String chat_id, String from_name, long user_id, String query_id) {
  // Accende la luce delle scale
  accendiLuce();
  
  // Risponde al callback con un messaggio di conferma
  bot.answerCallbackQuery(query_id, "💡 Luce Accesa! Si spegnerà automaticamente");
  
  // Crea il messaggio di conferma da inviare all'utente
  String message = "💡 *Luce Accesa*\n\n";
  message += "✅ Luce delle scale accesa\n";
  message += "⏰ Si spegnerà automaticamente!\n";
  message += "👤 Richiesta da: " + from_name;
  
  // Invia il messaggio di conferma tramite Telegram
  bot.sendMessage(chat_id, message, "Markdown");
  
  // Registra l'operazione nel log
  logOperation(user_id, "LUCE_ACCESA", from_name);
  
  // Aggiorna il display con lo stato corrente
  updateDisplay();
  
  // Stampa sul seriale per debugging
  Serial.println("✅ Luce accesa su richiesta di: " + from_name);
  
  // Riproduce un beep di conferma
  confirmationBeep();
  
  // Attende 1 secondo prima di inviare il menu principale
  delay(1000);
  
  // Invia il menu principale con i pulsanti interattivi
  sendMainMenu(chat_id);
}

/**
 * @brief Gestisce il callback del pulsante "Stato Sistema" nel menu Telegram.
 *        Mostra lo stato completo del sistema, incluyendo lo stato delle porte,
 *        delle luci e delle operazioni recenti.
 * @param chat_id ID della chat Telegram a cui rispondere
 * @param query_id ID della query Telegram
 */
void handleStatoSistemaCallback(String chat_id, String query_id) {
  // Rispondi al callback con un messaggio di conferma
  bot.answerCallbackQuery(query_id, "📊 Recupero stato sistema...");
  
  // Ottiene lo stato dettagliato del sistema
  String status = getDetailedSystemStatus();
  
  // Invia lo stato del sistema al chat_id specificato in formato Markdown
  bot.sendMessage(chat_id, status, "Markdown");
  
  // Attende 1 secondo prima di inviare il menu principale
  delay(1000);
  
  // Invia il menu principale con i pulsanti interattivi
  sendMainMenu(chat_id);
}

/**
 * @brief Gestisce il callback del pulsante "Mostra Log" nel menu Telegram.
 *        Mostra le ultime operazioni registrate nel sistema.
 *        Recupera il registro delle operazioni formattato e lo invia al chat_id specificato.
 * 
 * @param chat_id ID della chat Telegram a cui inviare il registro delle operazioni
 * @param query_id ID della query Telegram
 */
void handleMostraLogCallback(String chat_id, String query_id) {
  // Rispondi al callback con un messaggio di conferma
  bot.answerCallbackQuery(query_id, "📋 Recupero log operazioni...");
  
  // Ottiene il registro delle operazioni formattato
  String logStr = getFormattedLog();
  
  // Invia il registro delle operazioni al chat_id specificato in formato Markdown
  bot.sendMessage(chat_id, logStr, "Markdown");
  
  // Attende 1 secondo prima di inviare il menu principale
  delay(1000);
  
  // Invia il menu principale con i pulsanti interattivi
  sendMainMenu(chat_id);
}

/**
 * @brief Gestisce il callback del pulsante "Aiuto" nel menu Telegram.
 *        Il pulsante "Aiuto" è disponibile nel menu principale e consente
 *        all'utente di visualizzare la guida completa dei comandi del sistema.
 *        La guida completa include informazioni sui comandi principali, informativi,
 *        sulla sicurezza e sull'uso del sistema.
 * 
 * @param chat_id ID della chat Telegram a cui rispondere
 * @param query_id ID della query Telegram
 */
void handleAiutoCallback(String chat_id, String query_id) {
  // Rispondi al callback con un messaggio di conferma
  bot.answerCallbackQuery(query_id, "❓ Guida ai comandi...");
  
  // Invia la guida completa al chat_id specificato
  sendHelpMessage(chat_id);
}



// ================ FUNZIONI DI LOGGING DELLE OPERAZIONI ================

/**
 * @brief Inizializza il registro delle operazioni con valori vuoti.
 *        Il registro delle operazioni (log) contiene le ultime 5 operazioni
 *        eseguite dal sistema, complete di timestamp, ID della chat Telegram
 *        dell'utente che ha eseguito l'operazione, tipo di operazione e nome
 *        dell'utente che ha effettuato l'operazione.
 */
void initializeLog() {
  for (int i = 0; i < 5; i++) {
    // Svuota l'elemento corrente del registro
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
  // in modo da creare spazio per la nuova operazione
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
 *
 * Il registro delle operazioni contiene le ultime 5 operazioni eseguite dal sistema,
 * complete di timestamp, tipo di operazione e nome dell'utente che ha effettuato
 * l'operazione. La stringa formattata contiene emoji e simboli per facilitare la
 * comprensione del log.
 *
 * @return Stringa formattata con il registro delle operazioni
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
 *
 * La stringa formattata contiene informazioni sulla connettività WiFi,
 * lo stato dei dispositivi (porta e luce), informazioni di sistema (uptime,
 * memoria libera e numero di utenti autorizzati).
 *
 * @return Stringa formattata con lo stato del sistema
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
 * 
 * @param milliseconds Tempo trascorso in millisecondi
 * 
 * @return Stringa formattata con il tempo trascorso
 */
String formatUptime(unsigned long milliseconds) {
  unsigned long seconds = milliseconds / 1000; // Converti millisecondi in secondi
  unsigned long minutes = seconds / 60;        // Converti secondi in minuti
  unsigned long hours = minutes / 60;          // Converti minuti in ore
  unsigned long days = hours / 24;             // Converti ore in giorni

  String uptime = ""; // Stringa per accumulare il risultato formattato

  // Aggiungi giorni se presenti
  if (days > 0) uptime += String(days) + "g ";
  // Aggiungi ore rimanenti se presenti
  if (hours % 24 > 0) uptime += String(hours % 24) + "h ";
  // Aggiungi minuti rimanenti se presenti
  if (minutes % 60 > 0) uptime += String(minutes % 60) + "m ";
  // Aggiungi secondi rimanenti
  uptime += String(seconds % 60) + "s";

  return uptime; // Ritorna la stringa formattata
}

/**
 * @brief Formatta un intervallo temporale in formato leggibile.
 *        Es: "3 minuti fa", "2 ore fa", ecc.
 *        Utilizzato per mostrare l'ultima volta che un utente ha eseguito
 *        un'operazione.
 * 
 * @param ago Tempo trascorso in millisecondi
 * 
 * @return Stringa formattata con l'intervallo temporale
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
 * 
 * @details La funzione accende il relè della porta e imposta il
 *          timestamp dell'ultima volta che e' stata aperta.
 *          Utilizzato per tenere traccia dell'ultima volta che
 *          l'utente ha aperto la porta.
 */
void apriPorta() {
  digitalWrite(RELAY_PORTA, HIGH);
  portaOpenTime = millis();
  Serial.println("🚪 Porta aperta");
}

/**
 * @brief Accende la luce scale attivando il relè corrispondente.
 *        Imposta il timestamp dell'ultima accensione.
 * 
 * @details La funzione accende il relè della luce scale e imposta il
 *          timestamp dell'ultima volta che e' stata accesa.
 *          Utilizzato per tenere traccia dell'ultima volta che
 *          l'utente ha acceso la luce scale.
 */
void accendiLuce() {
  digitalWrite(RELAY_LUCE, HIGH);
  luceOnTime = millis();
  Serial.println("💡 Luce accesa");
}

/**
 * @brief Gestisce i timeout automatici per porta e luce.
 *        - Chiude la porta dopo un tempo prestabilito.
 *        - Spegne la luce dopo un tempo prestabilito.
 */
void handleTimeouts() {
  // ====== TIMEOUT PORTA ======
  // Controlla se la porta è aperta e se è trascorso il timeout
  if (portaOpenTime > 0 && millis() - portaOpenTime > PORTA_TIMEOUT) {
    // Chiude la porta disattivando il relè
    digitalWrite(RELAY_PORTA, LOW);
    portaOpenTime = 0; // Resetta il timestamp di apertura
    Serial.println("🚪 Porta chiusa automaticamente"); // Log su seriale
  }

  // ====== TIMEOUT LUCE ======
  // Controlla se la luce è accesa e se è trascorso il timeout
  if (luceOnTime > 0 && millis() - luceOnTime > LUCE_TIMEOUT) {
    // Spegne la luce disattivando il relè
    digitalWrite(RELAY_LUCE, LOW);
    luceOnTime = 0; // Resetta il timestamp di accensione
    Serial.println("💡 Luce spenta automaticamente"); // Log su seriale
  }
}

// =============== CONTROLLO AUTORIZZAZIONE UTENTI ==============
/**
 * @brief Verifica se un chat ID è nella lista degli utenti autorizzati.
 *        Utile per limitare l'accesso ai comandi Telegram.
 * 
 * @param chat_id ID della chat Telegram da verificare
 * @return true se l'utente è autorizzato, false altrimenti
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
 * 
 * @details La funzione riproduce una breve melodia utilizzando il buzzer
 *          per confermare che un'azione e' stata eseguita correttamente.
 *          Utile per confermare l'apertura della porta o l'accensione della luce.
 */
void confirmationBeep() {
  // Melodia di conferma positiva
  int melody[] = {587, 659, 523, 262, 392};
  // Durata di ogni nota
  int duration[] = {200, 200, 200, 200, 800};

  // Riproduce la melodia
  for (int i = 0; i < 5; i++) {
    tone(BUZZER_PIN, melody[i], duration[i]);
    delay(250);
  }

  // Disattiva il buzzer
  noTone(BUZZER_PIN);
}

/**
 * @brief Segnale acustico di successo .
 *        (es. connessione WiFi riuscita, Avvio di Sitema...)
 * Riproduce una breve melodia di tre note (Do, Mi, Sol) utilizzando il buzzer.
 * La durata di ogni nota e' di 200 millisecondi e c'e' un intervallo di 250
 * millisecondi tra ogni nota.
 */
void successBeep() {
  int melody[] = {262, 330, 392}; // Note: Do, Mi, Sol
  int duration[] = {200, 200, 200};

  // Riproduce la melodia
  for (int i = 0; i < 3; i++) {
    tone(BUZZER_PIN, melody[i], duration[i]);
    delay(250);
  }

  // Disattiva il buzzer
  noTone(BUZZER_PIN);
  delay(2000);
}

/**
 * @brief Segnale acustico di errore.
 *        E' utilizzato per segnalare un errore critico
 *        (es. password errata, accesso negato).
 */
void errorBeep() {
  // Suona un "beep" a 1000 Hz per 100 ms
  tone(BUZZER_PIN, 1000);
  delay(100);
  // Disattiva il buzzer
  noTone(BUZZER_PIN);
  // Pausa di 80 ms
  delay(80);
  // Suona un "beep" a 1500 Hz per 100 ms
  tone(BUZZER_PIN, 1500);
  delay(100);
  // Disattiva il buzzer
  noTone(BUZZER_PIN);
}

// ==================== LED DI STATO ====================
/**
 * @brief Lampeggia il LED blu integrato sull'ESP8266 per indicare che il sistema è attivo.
 *        Il LED lampeggia ogni secondo circa.
 * 
 * @details Utilizza una variabile statica per tenere traccia dell'ultimo
 *          lampeggio e un'altra per lo stato attuale del LED.
 *          Il LED cambia stato (acceso/spento) ogni secondo.
 */
void blinkStatusLED() {
  static unsigned long lastBlink = 0; // Memorizza il timestamp dell'ultimo lampeggio
  static bool ledState = false;       // Stato attuale del LED (acceso/spento)

  // Controlla se è trascorso più di un secondo dall'ultimo lampeggio
  if (millis() - lastBlink > 1000) {
    ledState = !ledState;                     // Inverte lo stato del LED
    digitalWrite(LED_STATUS, ledState ? HIGH : LOW); // Aggiorna lo stato del LED
    lastBlink = millis();                     // Aggiorna il timestamp dell'ultimo lampeggio
  }
}

// ================= CONTROLLO CONNESSIONE =================
/**
 * @brief Controlla periodicamente la connessione WiFi e tenta la riconnessione se necessario.
 *        Aggiorna lo stato del display OLED.
 * 
 * @details La funzione viene chiamata ogni 30 secondi e controlla se la connessione WiFi
 *          e' ancora attiva. Se la connessione WiFi e' disconnessa, tenta di riconnettersi
 *          e aggiorna lo stato del display OLED.
 */
void checkWiFiConnection() {
  static unsigned long lastCheck = 0; // Timestamp dell'ultimo controllo

  if (millis() - lastCheck > 30000) { // Ogni 30 secondi
    if (WiFi.status() != WL_CONNECTED) {
      // Se la connessione WiFi e' disconnessa, tenta di riconnettersi
      Serial.println("WiFi disconnesso, riconnessione...");
      WiFi.reconnect();
      wifiOk = false; // Aggiorna stato connessione
    } else {
      wifiOk = true; // Connessione OK
    }

    updateDisplay(); // Aggiorna il display OLED
    lastCheck = millis(); // Aggiorna timestamp dell'ultimo controllo
  }
}

