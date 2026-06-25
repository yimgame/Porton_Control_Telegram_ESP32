#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

// --- CONFIGURACIÓN WI-FI ---
const char* ssid = "tu wifi";
const char* password = "el pass de tu wifi";

// --- CONFIGURACIÓN TELEGRAM ---
const char* botToken = "tu token de tu bot de telegram lo creas con botfather";

// --- LISTA DE CHAT IDs PERMITIDOS ---
// Meté acá adentro los números de ID separados por coma.
const String usuariosAutorizados[] = {
  "123456789",  // Tu ID de telegram se obtiene con https://api.telegram.org/botTU_TOKEN/getUpdates lo abris en el navegador con tu token del bot
  // le escribis algo hola lo que sea al bot en telegram y le das F5 en la pc,te da el from id chat id algo id es un numero de 9 digitos
  //"987654321",  // ID de tu pareja / familiar
  //"555666777"   // Otro ID autorizado
};
const int CANTIDAD_USUARIOS = sizeof(usuariosAutorizados) / sizeof(usuariosAutorizados[0]);

// --- ASIGNACIÓN DE PINES ---
const int PIN_RELE_PODER = 5;       // Corta o da energía (Relé 1)
const int PIN_RELE_DIRECCION = 18;  // HIGH = Abrir, LOW = Cerrar (Relé 2)
const int SENSOR_ABIERTO = 19;      // Sensor final portón abierto (GND cuando toca)
const int SENSOR_CERRADO = 21;      // Sensor final portón cerrado (GND cuando toca)

// --- VARIABLES DE CONTROL ---
bool moviendo = false;
unsigned long tiempoUltimoMensaje;
unsigned long tiempoInicioMovimiento = 0;
const unsigned long TIMEOUT_MOTOR = 30000; // 30 segundos máximos de marcha por seguridad

WiFiClientSecure client;
UniversalTelegramBot bot(botToken, client);

// Función de seguridad para clavar el motor al instante
void frenarMotor() {
  digitalWrite(PIN_RELE_PODER, LOW); 
  digitalWrite(PIN_RELE_DIRECCION, LOW);
  moviendo = false;
  Serial.println("⏹️ [MOTOR] Detenido/Frenado de seguridad.");
}

void manejarNuevosMensajes(int numNuevosMensajes) {
  for (int i = 0; i < numNuevosMensajes; i++) {
    String chat_id = String(bot.messages[i].chat_id);
    String text = bot.messages[i].text;
    String nombre_usuario = bot.messages[i].from_name;

    // --- CÁLCULO DE FECHA Y HORA REAL DESDE TELEGRAM ---
    // Telegram nos da la hora en UTC (Londres). Ajustamos a nuestra zona horaria:
    const long DIFERENCIA_HORARIA = -3; // Ajuste para Argentina (UTC-3)
    
    // Obtenemos el tiempo en segundos y le sumamos o restamos las horas de diferencia
    time_t tiempoMensaje = bot.messages[i].date.toInt() + (DIFERENCIA_HORARIA * 3600);
    struct tm *timeinfo = gmtime(&tiempoMensaje); // Convierte a estructura de tiempo

    // Armamos la fecha (DD/MM/AA)
    char fechaFormateada[12];
    sprintf(fechaFormateada, "%02d/%02d/%02d", timeinfo->tm_mday, timeinfo->tm_mon + 1, (timeinfo->tm_year + 1900) % 100);

    // Armamos la hora (HH:MM:SS)
    char horaFormateada[10];
    sprintf(horaFormateada, "%02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);


    // --- CONTROL DE ACCESO MULTIUSUARIO ---
    bool accesoConcedido = false;
    for (int j = 0; j < CANTIDAD_USUARIOS; j++) {
      if (chat_id == usuariosAutorizados[j]) {
        accesoConcedido = true;
        break; 
      }
    }

    // --- CHISMOSO SERIAL CON FECHA Y HORA ---
    Serial.println("==================================================");
    Serial.print("📅 Fecha: "); Serial.print(fechaFormateada); Serial.print("  |  ⏰ Hora: "); Serial.println(horaFormateada);
    Serial.print("📩 Recibido de: "); Serial.println(nombre_usuario + " (ID: " + chat_id + ")");
    Serial.print("💬 Comando: "); Serial.println(text);
    Serial.print("🔐 Acceso: "); Serial.println(accesoConcedido ? "PERMITIDO ✅" : "DENEGADO ❌");
    Serial.println("==================================================");

    // Si el que escribe no está en la lista, lo rebota
    if (!accesoConcedido) {
      bot.sendMessage(chat_id, "No tenés permiso para controlar este portón. ❌", "");
      continue; 
    }

    // --- COMANDO ABRIR ---
    if (text == "/abrir") {
      if (digitalRead(SENSOR_ABIERTO) == LOW) {
        bot.sendMessage(chat_id, "El portón ya está completamente abierto. 🚗", "");
      } else if (moviendo) {
        bot.sendMessage(chat_id, "El portón ya se está moviendo. Primero usa /frenar.", "");
      } else {
        Serial.println("🎬 [ACCION] Ejecutando comando /abrir...");
        bot.sendMessage(chat_id, "Abriendo portón... 🔓", "");
        digitalWrite(PIN_RELE_DIRECCION, HIGH); 
        delay(100);                             
        digitalWrite(PIN_RELE_PODER, HIGH);    
        moviendo = true;
        tiempoInicioMovimiento = millis();
      }
    }

    // --- COMANDO CERRAR ---
    if (text == "/cerrar") {
      if (digitalRead(SENSOR_CERRADO) == LOW) {
        bot.sendMessage(chat_id, "El portón ya está completamente cerrado. 🏡", "");
      } else if (moviendo) {
        bot.sendMessage(chat_id, "El portón ya se está moviendo. Primero usa /frenar.", "");
      } else {
        Serial.println("🎬 [ACCION] Ejecutando comando /cerrar...");
        bot.sendMessage(chat_id, "Cerrando portón... 🔒", "");
        digitalWrite(PIN_RELE_DIRECCION, LOW); 
        delay(100);                            
        digitalWrite(PIN_RELE_PODER, HIGH);   
        moviendo = true;
        tiempoInicioMovimiento = millis();
      }
    }

    // --- COMANDO FRENAR ---
    if (text == "/frenar") {
      if (moviendo) {
        Serial.println("🎬 [ACCION] Ejecutando comando /frenar...");
        frenarMotor();
        bot.sendMessage(chat_id, "Portón frenado en su posición actual. 🛑", "");
      } else {
        bot.sendMessage(chat_id, "El motor ya estaba quieto.", "");
      }
    }

    // --- COMANDO ESTADO ---
    if (text == "/estado") {
      Serial.println("🎬 [ACCION] Enviando reporte de estado...");
      String estado = "Estado del portón:\n";
      estado += (moviendo) ? "🔄 En movimiento...\n" : "⏹️ Detenido.\n";
      estado += (digitalRead(SENSOR_ABIERTO) == LOW) ? "🟢 Totalmente Abierto\n" : "🔴 No está totalmente abierto\n";
      estado += (digitalRead(SENSOR_CERRADO) == LOW) ? "🟢 Totalmente Cerrado\n" : "🔴 No está totalmente cerrado\n";
      bot.sendMessage(chat_id, estado, "");
    }

    // --- COMANDO START / MENU ---
    if (text == "/start") {
      String bienvenido = "Control de Portón Seguro con ESP32:\n\n";
      bienvenido += "/abrir  - Abre el portón\n";
      bienvenido += "/cerrar - Cierra el portón\n";
      bienvenido += "/frenar - Detiene el motor de golpe\n";
      bienvenido += "/estado - Revisa los sensores";
      bot.sendMessage(chat_id, bienvenido, "");
    }
  }
}

void setup() {
  Serial.begin(115200);

  // Configuración de salidas (Relés)
  pinMode(PIN_RELE_PODER, OUTPUT);
  pinMode(PIN_RELE_DIRECCION, OUTPUT);
  frenarMotor(); // Asegura arrancar apagado

  // Configuración de entradas (Sensores con Pull-Up interna)
  pinMode(SENSOR_ABIERTO, INPUT_PULLUP);
  pinMode(SENSOR_CERRADO, INPUT_PULLUP);

  // Conexión Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  client.setInsecure(); // Saltea SSL para conectar directo a Telegram

  Serial.print("Conectando a Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n¡Wi-Fi Conectado exitosamente!");
}

void loop() {
  // --- SEGURIDAD 1: MONITOREO DE SENSORES EN TIEMPO REAL ---
  if (moviendo) {
    // Si abre y el imán toca el sensor de abierto -> FRENA
    if (digitalRead(PIN_RELE_DIRECCION) == HIGH && digitalRead(SENSOR_ABIERTO) == LOW) {
      frenarMotor();
      bot.sendMessage(usuariosAutorizados[0], "¡Portón ABIERTO por completo! 🚗", ""); // Avisa al admin principal
    }
    
    // Si cierra y el imán toca el sensor de cerrado -> FRENA
    if (digitalRead(PIN_RELE_DIRECCION) == LOW && digitalRead(SENSOR_CERRADO) == LOW) {
      frenarMotor();
      bot.sendMessage(usuariosAutorizados[0], "¡Portón CERRADO por completo! 🏡", ""); // Avisa al admin principal
    }

    // --- SEGURIDAD 2: TIMEOUT (Por si falla un imán) ---
    if (millis() - tiempoInicioMovimiento > TIMEOUT_MOTOR) {
      frenarMotor();
      bot.sendMessage(usuariosAutorizados[0], "🚨 ¡ALERTA! El motor superó el tiempo límite de marcha. Frenado por protección.", "");
    }
  }

  // --- REVISAR MENSAJES DE TELEGRAM CADA 1.5 SEGUNDOS ---
  if (millis() > tiempoUltimoMensaje + 1500)  {
    int numNuevosMensajes = bot.getUpdates(bot.last_message_received + 1);

    while(numNuevosMensajes) {
      manejarNuevosMensajes(numNuevosMensajes);
      numNuevosMensajes = bot.getUpdates(bot.last_message_received + 1);
    }
    tiempoUltimoMensaje = millis();
  }
}