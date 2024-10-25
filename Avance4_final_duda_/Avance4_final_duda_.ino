#include <TinyGPS++.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

// Definimos el GPS
TinyGPSPlus gps;
HardwareSerial ss(1);  // Usamos el segundo puerto UART (UART1)

// Parámetros WiFi
const char* ssid = "SmartStop";          // Nombre red WiFI
const char* password = "ElTiempoEsOro";  // Contraseña red WiFI

// Creación del servidor web
AsyncWebServer server(80);

// Variables de posición y velocidad del colectivo
double latitudColectivo, longitudColectivo;
double velocidadColectivo;  // En km/h

// Definir paradas fijas (latitud, longitud, nombre de la parada)
struct Parada {
  double latitud;
  double longitud;
  String nombre;
};

// Lista de paradas
Parada paradas[] = {
  { -27.457615, -58.978992, "Parada 1" },
  { -27.451946, -58.985443, "Parada 2" },
  { -27.457183, -58.993354, "Parada 3" }
};
const int totalParadas = sizeof(paradas) / sizeof(paradas[0]);

// Variables para el cálculo
unsigned long tiempoEstimadoMinutos = 0;
int indiceParadaMasCercana = 0;

// Función para calcular la distancia entre dos puntos GPS (en kilómetros)
double calcularDistancia(double lat1, double lon1, double lat2, double lon2) {
  const double radioTierra = 6371000;  // Radio de la Tierra en metros
  double dLat = (lat2 - lat1) * PI / 180.0;
  double dLon = (lon2 - lon1) * PI / 180.0;
  double a = sin(dLat / 2) * sin(dLat / 2) + cos(lat1 * PI / 180.0) * cos(lat2 * PI / 180.0) * sin(dLon / 2) * sin(dLon / 2);
  double c = 2 * atan2(sqrt(a), sqrt(1 - a));
  return radioTierra * c / 1000;  // Devuelve la distancia en kilómetros
}

// Función para calcular el tiempo estimado en minutos usando la velocidad actual
unsigned long calcularTiempoEstimado(double distancia, double velocidad) {
  if (velocidad > 0) {
    return (distancia / velocidad) * 60;  // Velocidad en km/h, tiempo en minutos
  }
  return 999;  // Si la velocidad es 0, tiempo estimado muy alto
}

// Función para encontrar la parada más cercana
int encontrarParadaMasCercana() {
  double distanciaMinima = 999999;
  int indiceCercano = 0;

  for (int i = 0; i < totalParadas; i++) {
    double distancia = calcularDistancia(latitudColectivo, longitudColectivo, paradas[i].latitud, paradas[i].longitud);
    if (distancia < distanciaMinima) {
      distanciaMinima = distancia;
      indiceCercano = i;
    }
  }
  return indiceCercano;
}

void setup() {
  Serial.begin(115200);

  // Inicia UART1 para el GPS (GPIO 16 = RX, GPIO 17 = TX)
  ss.begin(9600, SERIAL_8N1, 16, 17);

  // Conexión a la red WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Conectando a WiFi...");
  }
  Serial.println("Conectado a WiFi");

  // Configura el servidor web
  // Página principal
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    String html = "<html><head>";
    html += "<style>";
    html += "body { text-align: center; font-family: Arial, sans-serif; }";  // Centra el texto y establece la fuente
    html += "h1 { font-size: 4.5em; }";                                      // Título más grande
    html += "p { font-size: 3.0em; }";                                       // Texto de párrafos más grande
    html += "</style>";
    html += "</head><body>";
    html += "<h1>Datos del GPS</h1>";
    html += "<p id='latitud'>Latitud: " + String(latitudColectivo, 6) + "</p>";
    html += "<p id='longitud'>Longitud: " + String(longitudColectivo, 6) + "</p>";
    html += "<p id='velocidad'>Velocidad: -- km/h</p>";
    html += "<p id='paradaMasCercana'>Parada más cercana: --</p>";
    html += "<p id='distancia'>Distancia a la parada: -- km</p>";
    html += "<p id='tiempoLlegada'>Tiempo de llegada: -- minutos</p>";
    html += "<p id='estado'>Estado: --</p>";
    html += "<button onclick='updateData()'>Actualizar Datos</button>";  // Botón para actualizar
    html += "<script>";
    html += "function updateData() {";
    html += "fetch('/data').then(response => response.json()).then(data => {";
    html += "document.getElementById('latitud').innerHTML = 'Latitud: ' + data.latitud;";
    html += "document.getElementById('longitud').innerHTML = 'Longitud: ' + data.longitud;";
    html += "document.getElementById('velocidad').innerHTML = 'Velocidad: ' + data.velocidad + ' km/h';";
    html += "document.getElementById('paradaMasCercana').innerHTML = 'Parada más cercana: ' + data.parada;";
    html += "document.getElementById('distancia').innerHTML = 'Distancia a la parada: ' + data.distancia + ' km';";
    html += "document.getElementById('tiempoLlegada').innerHTML = 'Tiempo de llegada: ' + data.tiempo;";
    html += "document.getElementById('estado').innerHTML = 'Estado: ' + data.estado;";
    html += "});";
    html += "}";
    html += "setInterval(updateData, 2000);";  // Actualiza automáticamente cada 2 segundos
    html += "</script>";
    html += "</body></html>";
    request->send(200, "text/html", html);
  });

  // Ruta para devolver datos de GPS en formato JSON
  server.on("/data", HTTP_GET, [](AsyncWebServerRequest* request) {
    indiceParadaMasCercana = encontrarParadaMasCercana();
    Parada paradaCercana = paradas[indiceParadaMasCercana];

    double distancia = calcularDistancia(latitudColectivo, longitudColectivo, paradaCercana.latitud, paradaCercana.longitud);
    // Usar la velocidad actual obtenida del GPS
    double velocidadKmH = gps.speed.kmph();  // Convertir la velocidad a km/h

    unsigned long tiempoLlegada = calcularTiempoEstimado(distancia, velocidadKmH);

    String estado = "En línea";  // Mensaje por defecto
    String tiempoMostrado = String(tiempoLlegada) + " minutos";

    // Si el tiempo estimado es mayor a 60 minutos, mostrar "Colectivo demorado"
    if (tiempoLlegada > 60) {
      tiempoMostrado = "Colectivo demorado +60minutos";
      estado = "Demorado";  // Actualiza el estado a "Demorado"
    }

    String json = "{\"latitud\":" + String(latitudColectivo, 6) + ",\"longitud\":" + String(longitudColectivo, 6) +
                  ",\"velocidad\":\"" + String(velocidadKmH, 2) + "\",\"parada\":\"" + paradaCercana.nombre +
                  "\",\"distancia\":" + String(distancia, 2) + ",\"tiempo\":\"" + tiempoMostrado + "\",\"estado\":\"" + estado + "\"}";
    request->send(200, "application/json", json);
  });

  // Inicia el servidor
  server.begin();
}

void loop() {
  // Verificamos si el GPS está enviando datos
  while (ss.available() > 0) {
    gps.encode(ss.read());
    if (gps.location.isUpdated()) {
      latitudColectivo = gps.location.lat();
      longitudColectivo = gps.location.lng();
    }
    if (gps.speed.isUpdated()) {
      velocidadColectivo = gps.speed.kmph();  // Obtener velocidad en km/h
    }

    // Muestra los datos en el monitor serie para depuración
    Serial.print("Latitud: ");
    Serial.println(latitudColectivo, 6);
    Serial.print("Longitud: ");
    Serial.println(longitudColectivo, 6);
    Serial.print("Velocidad: ");
    Serial.println(velocidadColectivo, 2);
  }
}
