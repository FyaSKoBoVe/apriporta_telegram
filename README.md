 
# 📄 ESP8266 D1 Mini – Sistema Apriporta con Telegram Bot e OLED

## Indice

- [Descrizione](#descrizione)
- [Funzionalità principali](#funzionalità-principali)
- [Requisiti Hardware](#requisiti-hardware)
- [Schema collegamenti (per D1 mini)](#schema-collegamenti-per-d1-mini)
- [Installazione Librerie Necessarie (via Library Manager)](#installazione-librerie-necessarie-via-library-manager)
- [Configurazione iniziale di Telegram](#configurazione-iniziale-di-telegram)
- [Configurazione al primo avvio](#configurazione-al-primo-avvio)
- [Funzionalità principali - Dettaglio](#funzionalità-principali---dettaglio)
- [Comandi Disponibili](#comandi-disponibili)
- [Sicurezza](#sicurezza)
- [Registrazione Operazioni (Log)](#registrazione-operazioni-log)
- [Feedback Sonoro](#feedback-sonoro)
- [Esempi di Output](#esempi-di-output)
- [Aggiornamenti e Personalizzazione](#aggiornamenti-e-personalizzazione)
- [Reset/Recovery](#resetrecovery)
- [Licenza](#licenza)
- [Credits](#credits)

---

## Descrizione
📋

Questo progetto trasforma un **ESP8266 D1 mini** in un sistema di controllo remoto per **citofono/apriporta**, gestibile sia via **pulsante fisico** che tramite **bot Telegram**. Il sistema offre anche controllo **luce scale**, **visualizzazione stato/log su display OLED** e una **pagina web integrata** per configurazione WiFi, token del bot e utenti autorizzati.

---

## Funzionalità principali
📋

- **Apertura porta** da pulsante fisico o da Telegram
- **Accensione luce scale** da comando Telegram
- **Display OLED** (0.92" SSD1306 128x64) con stato e ultime operazioni
- **Pagina web captive portal** per configurare WiFi e token del bot
- **Gestione utenti autorizzati** tramite chat ID Telegram
- **Log delle ultime 5 operazioni**
- **Controllo accessi** via web tramite password

---

## Requisiti Hardware
🔧

| Componente             | Descrizione |
|------------------------|-------------|
| ESP8266 D1 Mini        | Microcontrollore principale |
| Relè Porta             | 5v |
| Relè Luce              | 5v |
| LED Status             | LED blu integrato su `D4 (GPIO2)` |
| Buzzer                 | Opzionale|
| Display OLED SSD1306   | Collegato via I2C a `D1 (GPIO5)` e `D2 (GPIO4)` |
| Pulsante Apriporta     | Normalmente  Aperto (NO) |
| Pulsante Reset Config  | Normalmente  Aperto (NO) |
| Alimentazione          | 3.3V o 5V a seconda dei componenti |

---

## Schema collegamenti (per D1 mini)
🔧

| Pin D1 mini | Collegamento       | Descrizione                        |
|-------------|--------------------|------------------------------------|
| D5 (GPIO14) | Relè Porta         | Attivazione apertura porta         |
| D8 (GPIO15) | Relè Luce          | Accensione luce scale              |
| D4 (GPIO2)  | LED Status         | LED integrato su D1 mini           |
| D7 (GPIO13) | Buzzer (opzionale) | Segnalazione acustica              |
| D1 (GPIO5)  | SCL OLED Display   | Comunicazione I2C                  |
| D2 (GPIO4)  | SDA OLED Display   | Comunicazione I2C                  |
| D3 (GPIO0)  | Pulsante Porta     | NO tra GPIO0 e GND                 |
| D0 (GPIO16) | Pulsante Config    | NO tra GPIO16 e GND (modalità conf)|
| 3.3V/5V     | VCC Relè           | Alimentazione relè                 |
| 3.3V        | VCC OLED           | Alimentazione display              |
| GND         | GND                | Massa comune                       |

---

## Installazione Librerie Necessarie (via Library Manager)
📦

Assicurati di installare queste librerie prima di caricare il codice:

- [UniversalTelegramBot](https://github.com/witnessmenow/Universal-Arduino-Telegram-Bot) by Brian Lough
- [ArduinoJson](https://arduinojson.org/) by Benoit Blanchon (versione 6.x)
- [Adafruit_GFX](https://github.com/adafruit/Adafruit-GFX-Library)
- [Adafruit_SSD1306](https://github.com/adafruit/Adafruit_SSD1306)
- [ESP8266WiFi](https://arduino-esp8266.readthedocs.io/en/latest/esp8266wifi/readme.html)
- [ESP8266WebServer](https://arduino-esp8266.readthedocs.io/en/latest/esp8266webserver/readme.html)
- [DNSServer](https://github.com/zaklaus/ESPAsyncUDP/tree/master/src/DNSServer)

---

## Configurazione iniziale di Telegram
⚙️

1. **Crea il bot Telegram**
    - Scrivi a [@BotFather](https://telegram.me/BotFather) su Telegram
    - Usa `/newbot` e segui le istruzioni
    - Copia il token fornito
2. **Trova chat ID**
    - Invia il Bot a chi lo potrà utilizzare
    - Ogni utilizzatore dovrà mandarti il suo ID
      - Vai su `https://api.telegram.org/bot<TUO_TOKEN>/getUpdates`
      - Cerca `"chat":{"id":NUMERO}` e annota il numero
      - In alternativa, usa [@MyIDBot](https://telegram.me/myidbot) e invia `/getid` (consigliato)
      - Copia tutti gli ID
3. **Imposta credenziali WiFi e Telegram nel codice**
    - Modifica le variabili `ssid`, `password`, e `BOT_TOKEN`
    - Inserisci i chat ID autorizzati nell’array `authorizedUsers`
4. **Collega l’hardware** come da schema sopra

---

## Configurazione al primo avvio
⚙️

1. Al primo avvio (o in caso di errore WiFi) il dispositivo crea una WiFi "**ESP_Config**".
2. Collegati con PC/smartphone e vai su **192.168.4.1**.
3. **Configurazione Web**
    - La pagina web richiede una password per accedere alla configurazione (default: **admin**).
    - Pagina web per inserire:
      - SSID e Password WiFi
      - Token del Bot Telegram
      - Chat ID degli utenti autorizzati (separati da virgole, esempio: `12345678,87654321`)
      - Cambio password per accedere alla configurazione
    - Possibilità di **resettare il dispositivo**
4. Salva e riavvia (automaticamente).

Le accensioni successive:

- Il programma legge i dati salvati nella **EEPROM**.
- Se non sono presenti dati validi o se si verifica un errore di connessione:
    - Si entra in **modalità AP** creando una rete WiFi chiamata "**ESP_Config**".
    - Si avvia un server web su `http://192.168.4.1` per la configurazione iniziale.

---

## Funzionalità principali - Dettaglio
🌐

- **Controllo remoto tramite Telegram**
  - `/apri` – Apertura della porta
  - `/luce` – Accensione della luce scale
  - `/stato` – Visualizza lo stato attuale del sistema
  - `/log` – Mostra le ultime operazioni registrate
  - `/help` – Guida completa ai comandi
  - `/menu` – Menu interattivo con pulsanti inline

- **Sicurezza**
  - Solo gli utenti autorizzati possono eseguire comandi
  - Tutte le operazioni vengono registrate nel log

- **Display OLED**
  - Mostra stato WiFi, connessione Telegram
  - Ultimi 5 eventi registrati
  - Titolo e stato del sistema in tempo reale

---

## Comandi Disponibili
🎮

### Comandi Testuali

| Comando      | Azione |
|--------------|--------|
| `/start`     | Messaggio di benvenuto |
| `/apri`      | Apre la porta |
| `/luce`      | Accende la luce scale |
| `/stato`     | Mostra stato completo del sistema |
| `/log`       | Mostra le ultime operazioni registrate |
| `/help`      | Mostra la guida completa |
| `/menu`      | Mostra il menu interattivo con pulsanti |

### Pulsanti Inline Telegram

Il menu interattivo permette di controllare il sistema con pochi tap:

- **🚪 Apri Porta**
- **💡 Accendi Luce**
- **ℹ️ Stato Sistema**
- **📋 Mostra Log**
- **❓ Aiuto**

---

## Sicurezza
🔒

- Il sistema **blocca l’accesso ai comandi** a tutti tranne agli utenti con chat ID autorizzato.
- Ogni azione eseguita viene registrata nel log insieme al nome dell’utente.
- La pagina web richiede una password per accedere alla configurazione (default: `admin`).

---

## Registrazione Operazioni (Log)
📈

Il sistema tiene traccia delle ultime **5 operazioni**:

- Tipo di operazione (`PORTA_APERTA`, `LUCE_ACCESA`, ecc.)
- Nome dell'utente
- Timestamp relativo (es. “3m fa”)

Mostrato sia sul display che inviato via Telegram con il comando `/log`.

---

## Feedback Sonoro
🔊

Opzionalmente, il sistema può emettere segnali sonori:

- **Beep singolo**: conferma comando eseguito
- **Melodia di avvio**: sistema acceso e pronto
- **Segnale di errore**: accesso negato o password errata

---

## Esempi di Output

### Display OLED

```
D1 Mini Apriporta
Pulsante Porta 3m
UtenteX Luce 1h
UtenteY Porta 5h
WiFi:OK TG:OK
```

### Log su Telegram.

```
📋 Registro Operazioni
• 🚪 PORTA APERTA
  👤 UtenteX
  ⏰ 3 minuti fa
• 💡 LUCE ACCESA
  👤 UtenteY
  ⏰ 1 ora fa
```

---

## Aggiornamenti e Personalizzazione.
🔄
Puoi personalizzare:

- Durata apertura porta e accensione luce (`PORTA_TIMEOUT`, `LUCE_TIMEOUT`)
- Melodie e feedback sonori
- Dimensione e posizione del testo sul display
- Interfaccia web (HTML/CSS)

`SOLAMENTE MODIFICANDO IL PROGRAMMA`

---

## Reset/Recovery.
⚙️

Premendo il **pulsante di reset** (**GPIO16**) per più di 5 secondi:

- Il sistema entra nuovamente in modalità configurazione (AP).
- Si apre la richiesta "Password"
- Nella stessa pagina è possibile **resettare la EEPROM** a stato di fabbrica (“Reset EEPROM”).
- Una volta inserita la password e premuto "Accedi"
- È possibile reimpostare SSID, token o lista utenti direttamente dal browser.
- Da qui è possibile accedere anche al cambio Password. 

---


## Licenza.

Questo progetto è rilasciato sotto licenza open source [GPLv3](https://www.gnu.org/licenses/gpl-3.0.html). Puoi usarlo, modificarlo e condividerlo liberamente.

---

## Credits.

Realizzato per ESP8266 D1 mini – progetto Apriporta [ver. 01].

### Autore

[FyaSKoBoVe](https://github.com/FyaSKoBoVe/)
