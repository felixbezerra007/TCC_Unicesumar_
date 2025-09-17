
/*
TCC ENGENHARIA ELÉTRICA - UNICESUMAR 
ANO: SETEMBRO / 2025
ALUNO FELIX NEVES BEZERRA
RA: 24010195-5
Projeto de Sistema Inteligente de Monitoramento e Controle do Consumo de Energia Elétrica Residencial Utilizando ESP32 com Interface Web em Tempo Real
Versão com Controle de Relés
*/
#include <WiFi.h>
#include <WebServer.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <EEPROM.h>

// Configurações WiFi
const char* ssid = "Nina"; //SEU_WIFI";
const char* password = "Vl@1810-N"; //"SUA_SENHA";

// Configuração do LCD I2C
LiquidCrystal_I2C lcd(0x27, 20, 4); // Endereço I2C padrão 0x27

// Pinos dos sensores
const int CURRENT_PIN = 34;  // SCT-013-100 (pino ADC)
const int VOLTAGE_PIN = 35;  // ZMPT101B (pino ADC)

// Pinos dos relés
const int RELAY_CHUVEIRO = 17;  // Relé do chuveiro
const int RELAY_AR_CONDICIONADO = 16;  // Relé do ar condicionado

// Servidor Web
WebServer server(80);

// Variáveis globais
float voltage = 0.0;
float current = 0.0;
float power = 0.0;
float energy = 0.0; // kWh
float totalEnergy = 0.0;

// Variáveis de controle dos relés
bool relayChuveiro = true;  // Estado do relé do chuveiro (true = ligado)
bool relayArCondicionado = true;  // Estado do relé do ar condicionado (true = ligado)

// Limites de energia para desligamento automático
const float LIMITE_CHUVEIRO = 100.0;  // 100 kWh
const float LIMITE_AR_CONDICIONADO = 150.0;  // 150 kWh

// Calibração (ajustar conforme necessário)
const float VOLTAGE_CALIBRATION = 1875.0; // Ajustar baseado na tensão real (bateu 110V=2115)
const float CURRENT_CALIBRATION = 30.0;   // Para SCT-013-100 com burden 33Ω
const float PHASE_SHIFT = 1.7; // Correção de fase

// Controle de tempo
unsigned long lastTime = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastEnergyUpdate = 0;

// Buffer para cálculos RMS
const int SAMPLES = 1000;
int sampleI = 0;
int sampleV = 0;

void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);
  
  // Configurar pinos dos relés
  pinMode(RELAY_CHUVEIRO, OUTPUT);
  pinMode(RELAY_AR_CONDICIONADO, OUTPUT);
  
  // Inicializar relés (ligados por padrão)
  digitalWrite(RELAY_CHUVEIRO, HIGH);
  digitalWrite(RELAY_AR_CONDICIONADO, HIGH);
  
  // Inicializar LCD
  lcd.begin();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Medidor de Energia");
  lcd.setCursor(0, 1);
  lcd.print("Iniciando...");
  
  // Conectar WiFi
  WiFi.begin(ssid, password);
  lcd.setCursor(0, 2);
  lcd.print("Conectando WiFi...");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Conectando ao WiFi...");
  }
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi Conectado!");
  lcd.setCursor(0, 1);
  lcd.print("IP: ");
  lcd.print(WiFi.localIP().toString());
  delay(2000);
  
  // Configurar servidor web
  setupWebServer();
  server.begin();
  
  // Carregar energia total da EEPROM
  EEPROM.get(0, totalEnergy);
  if (isnan(totalEnergy)) {
    totalEnergy = 0.0;
  }
  
  // Carregar estado dos relés da EEPROM
  EEPROM.get(4, relayChuveiro);
  EEPROM.get(5, relayArCondicionado);
  
  // Aplicar estado dos relés
  digitalWrite(RELAY_CHUVEIRO, relayChuveiro ? HIGH : LOW);
  digitalWrite(RELAY_AR_CONDICIONADO, relayArCondicionado ? HIGH : LOW);
  
  Serial.println("Sistema iniciado!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
  Serial.printf("Estado inicial - Chuveiro: %s, Ar Condicionado: %s\n", 
                relayChuveiro ? "ON" : "OFF", 
                relayArCondicionado ? "ON" : "OFF");
}

void loop() {
  server.handleClient();
  
  // Ler sensores a cada 100ms
  if (millis() - lastTime > 100) {
    readSensors();
    lastTime = millis();
  }
  
  // Atualizar display a cada 1 segundo
  if (millis() - lastDisplayUpdate > 1000) {
    updateDisplay();
    lastDisplayUpdate = millis();
  }
  
  // Atualizar energia a cada 10 segundos
  if (millis() - lastEnergyUpdate > 10000) {
    updateEnergy();
    checkEnergyLimits();  // Verificar limites de energia
    lastEnergyUpdate = millis();
  }
}

void readSensors() {
  // Ler múltiplas amostras para cálculo RMS
  long sumI = 0;
  long sumV = 0;
  long sumP = 0;
  
  for (int n = 0; n < SAMPLES; n++) {
    sampleI = analogRead(CURRENT_PIN);
    sampleV = analogRead(VOLTAGE_PIN);
    
    // Converter para valores centrados em zero
    int offsetI = 2048; // Offset para ESP32 (12-bit ADC) - SCT-013
    int offsetV = 2048; // Offset para ESP32 (12-bit ADC) - ZMPT101B
    
    double filteredI = sampleI - offsetI;
    double filteredV = sampleV - offsetV;
    
    // Quadrado das amostras para RMS
    sumI += (filteredI * filteredI);
    sumV += (filteredV * filteredV);
    
    // Para cálculo de potência real (considerando fase)
    sumP += (filteredI * filteredV);
    
    delayMicroseconds(400); // ~2.5kHz sampling rate
  }
  
  // Cálculo RMS
  double Irms = sqrt((double)sumI / SAMPLES);
  double Vrms = sqrt((double)sumV / SAMPLES);
  
  // Aplicar calibração
  voltage = Vrms * VOLTAGE_CALIBRATION / 4096.0;
  current = Irms * CURRENT_CALIBRATION / 4096.0;
  
  // Corrigir valores baixos de corrente (ruído)
  if (current < 0.1) {
    current = 0.0;
  }
  
  // Calcular potência real
  double realPower = (sumP / SAMPLES) * VOLTAGE_CALIBRATION * CURRENT_CALIBRATION / (4096.0 * 4096.0);
  power = abs(realPower);
  
  // Se corrente é zero, potência também é zero
  if (current == 0.0) {
    power = 0.0;
  }
  
  // Debug adicional para calibração
  Serial.printf("DEBUG - Vrms_raw: %.1f, V_calc: %.1fV | Irms_raw: %.1f, I_calc: %.2fA\n", 
                Vrms, voltage, Irms, current);
}

void updateDisplay() {
  lcd.clear();
  
  // Linha 1: Tensão
  lcd.setCursor(0, 0);
  lcd.print("V:");
  lcd.print(voltage, 1);
  lcd.print("V");
  
  // Mostrar IP no lado direito da primeira linha
  String ip = WiFi.localIP().toString();
  int ipLength = ip.length();
  if (ipLength <= 12) { // Se IP cabe na linha
    lcd.setCursor(20 - ipLength, 0);
    lcd.print(ip);
  }
  
  // Linha 2: Corrente e Potência
  lcd.setCursor(0, 1);
  lcd.print("I:");
  lcd.print(current, 2);
  lcd.print("A P:");
  lcd.print(power, 0);
  lcd.print("W");
  
  // Linha 3: Energia total
  lcd.setCursor(0, 2);
  lcd.print("E:");
  lcd.print(totalEnergy, 2);
  lcd.print("kWh");
  
  // Linha 4: Status dos relés
  lcd.setCursor(0, 3);
  lcd.print("CHU:");
  lcd.print(relayChuveiro ? "ON " : "OFF");
  lcd.print(" AC:");
  lcd.print(relayArCondicionado ? "ON " : "OFF");
}

void updateEnergy() {
  // Calcular energia consumida nos últimos 10 segundos
  double energyDelta = (power * 10.0) / 3600000.0; // Converter W*s para kWh
  totalEnergy += energyDelta;
  
  // Salvar na EEPROM a cada atualização
  EEPROM.put(0, totalEnergy);
  EEPROM.commit();
  
  // Debug
  Serial.printf("V: %.1fV, I: %.2fA, P: %.1fW, E: %.3fkWh\n", 
                voltage, current, power, totalEnergy);
}

void checkEnergyLimits() {
  bool estadoAnteriorChuveiro = relayChuveiro;
  bool estadoAnteriorAr = relayArCondicionado;
  
  // Verificar limite do chuveiro (100 kWh)
  if (totalEnergy >= LIMITE_CHUVEIRO && relayChuveiro) {
    relayChuveiro = false;
    digitalWrite(RELAY_CHUVEIRO, LOW);
    Serial.println("ALERTA: Chuveiro desligado automaticamente - Limite de 100 kWh atingido!");
  }
  
  // Verificar limite do ar condicionado (150 kWh)
  if (totalEnergy >= LIMITE_AR_CONDICIONADO && relayArCondicionado) {
    relayArCondicionado = false;
    digitalWrite(RELAY_AR_CONDICIONADO, LOW);
    Serial.println("ALERTA: Ar condicionado desligado automaticamente - Limite de 150 kWh atingido!");
  }
  
  // Salvar estado dos relés na EEPROM se houve mudança
  if (estadoAnteriorChuveiro != relayChuveiro || estadoAnteriorAr != relayArCondicionado) {
    EEPROM.put(4, relayChuveiro);
    EEPROM.put(5, relayArCondicionado);
    EEPROM.commit();
  }
}

void setupWebServer() {
  // Página principal
  server.on("/", []() {
    String html = generateWebPage();
    server.send(200, "text/html", html);
  });
  
  // API para dados JSON
  server.on("/data", []() {
    String json = "{";
    json += "\"voltage\":" + String(voltage, 1) + ",";
    json += "\"current\":" + String(current, 2) + ",";
    json += "\"power\":" + String(power, 1) + ",";
    json += "\"energy\":" + String(totalEnergy, 3) + ",";
    json += "\"relayChuveiro\":" + String(relayChuveiro ? "true" : "false") + ",";
    json += "\"relayArCondicionado\":" + String(relayArCondicionado ? "true" : "false") + ",";
    json += "\"limiteChuveiro\":" + String(LIMITE_CHUVEIRO, 1) + ",";
    json += "\"limiteArCondicionado\":" + String(LIMITE_AR_CONDICIONADO, 1);
    json += "}";
    
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
  });
  
  // Controle manual do relé do chuveiro
  server.on("/chuveiro/toggle", []() {
    if (totalEnergy < LIMITE_CHUVEIRO) {
      relayChuveiro = !relayChuveiro;
      digitalWrite(RELAY_CHUVEIRO, relayChuveiro ? HIGH : LOW);
      EEPROM.put(4, relayChuveiro);
      EEPROM.commit();
      server.send(200, "text/plain", relayChuveiro ? "Chuveiro ligado" : "Chuveiro desligado");
    } else {
      server.send(400, "text/plain", "Não é possível ligar - limite de energia atingido");
    }
  });
  
  // Controle manual do relé do ar condicionado
  server.on("/ar/toggle", []() {
    if (totalEnergy < LIMITE_AR_CONDICIONADO) {
      relayArCondicionado = !relayArCondicionado;
      digitalWrite(RELAY_AR_CONDICIONADO, relayArCondicionado ? HIGH : LOW);
      EEPROM.put(5, relayArCondicionado);
      EEPROM.commit();
      server.send(200, "text/plain", relayArCondicionado ? "Ar condicionado ligado" : "Ar condicionado desligado");
    } else {
      server.send(400, "text/plain", "Não é possível ligar - limite de energia atingido");
    }
  });
  
  // Reset energia
  server.on("/reset", []() {
    totalEnergy = 0.0;
    // Religar os relés quando resetar a energia
    relayChuveiro = true;
    relayArCondicionado = true;
    digitalWrite(RELAY_CHUVEIRO, HIGH);
    digitalWrite(RELAY_AR_CONDICIONADO, HIGH);
    
    EEPROM.put(0, totalEnergy);
    EEPROM.put(4, relayChuveiro);
    EEPROM.put(5, relayArCondicionado);
    EEPROM.commit();
    
    server.send(200, "text/plain", "Energia resetada e relés religados!");
  });
}

String generateWebPage() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Medidor de Energia ESP32</title>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        body {
            font-family: Arial, sans-serif;
            max-width: 1000px;
            margin: 0 auto;
            padding: 20px;
            background-color: #f0f0f0;
        }
        .container {
            background: white;
            border-radius: 10px;
            padding: 20px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
        }
        .header {
            text-align: center;
            color: #333;
            margin-bottom: 30px;
        }
        .meter-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
            gap: 20px;
            margin-bottom: 30px;
        }
        .meter {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 20px;
            border-radius: 10px;
            text-align: center;
            box-shadow: 0 4px 15px rgba(0,0,0,0.1);
        }
        .meter-value {
            font-size: 2em;
            font-weight: bold;
            margin: 10px 0;
        }
        .meter-unit {
            font-size: 0.9em;
            opacity: 0.8;
        }
        .relay-section {
            margin: 30px 0;
        }
        .relay-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
            gap: 20px;
        }
        .relay-card {
            background: white;
            border-radius: 10px;
            padding: 20px;
            border: 2px solid #ddd;
            transition: all 0.3s ease;
        }
        .relay-card.on {
            border-color: #4CAF50;
            background: #f8fff8;
        }
        .relay-card.off {
            border-color: #f44336;
            background: #fff8f8;
        }
        .relay-card.blocked {
            border-color: #ff9800;
            background: #fff8e1;
        }
        .relay-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 15px;
        }
        .relay-title {
            font-size: 1.2em;
            font-weight: bold;
        }
        .relay-status {
            padding: 5px 15px;
            border-radius: 20px;
            color: white;
            font-weight: bold;
        }
        .status-on { background: #4CAF50; }
        .status-off { background: #f44336; }
        .status-blocked { background: #ff9800; }
        .relay-info {
            margin: 10px 0;
            color: #666;
        }
        .progress-bar {
            width: 100%;
            height: 20px;
            background: #eee;
            border-radius: 10px;
            overflow: hidden;
            margin: 10px 0;
        }
        .progress-fill {
            height: 100%;
            transition: width 0.3s ease;
        }
        .progress-chuveiro { background: #2196F3; }
        .progress-ar { background: #FF9800; }
        .controls {
            text-align: center;
            margin-top: 20px;
        }
        button {
            border: none;
            padding: 12px 24px;
            border-radius: 5px;
            cursor: pointer;
            font-size: 16px;
            margin: 5px;
            transition: background-color 0.3s;
        }
        .btn-toggle {
            background: #2196F3;
            color: white;
        }
        .btn-toggle:hover { background: #1976D2; }
        .btn-toggle:disabled {
            background: #ccc;
            cursor: not-allowed;
        }
        .btn-reset {
            background: #f44336;
            color: white;
        }
        .btn-reset:hover { background: #d32f2f; }
        .status {
            text-align: center;
            margin-top: 10px;
            color: #666;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>🔌 Medidor de Energia ESP32</h1>
            <p>Trabalho de Conclusão de Curso Engenharia Elétrica - UNICESUMAR</p>
            <p>ALUNO: Felix Neves Bezerra</p>
        </div>
        
        <div class="meter-grid">
            <div class="meter">
                <div class="meter-label">Tensão</div>
                <div class="meter-value" id="voltage">--</div>
                <div class="meter-unit">Volts</div>
            </div>
            
            <div class="meter">
                <div class="meter-label">Corrente</div>
                <div class="meter-value" id="current">--</div>
                <div class="meter-unit">Amperes</div>
            </div>
            
            <div class="meter">
                <div class="meter-label">Potência</div>
                <div class="meter-value" id="power">--</div>
                <div class="meter-unit">Watts</div>
            </div>
            
            <div class="meter">
                <div class="meter-label">Energia Total</div>
                <div class="meter-value" id="energy">--</div>
                <div class="meter-unit">kWh</div>
            </div>
        </div>
        
        <div class="relay-section">
            <h2>🎛️ Controle de Cargas</h2>
            <div class="relay-grid">
                <div class="relay-card" id="card-chuveiro">
                    <div class="relay-header">
                        <span class="relay-title">🚿 Chuveiro</span>
                        <span class="relay-status" id="status-chuveiro">--</span>
                    </div>
                    <div class="relay-info">
                        Limite: 100.0 kWh
                    </div>
                    <div class="progress-bar">
                        <div class="progress-fill progress-chuveiro" id="progress-chuveiro" style="width: 0%"></div>
                    </div>
                    <div class="relay-info" id="info-chuveiro">--</div>
                    <button class="btn-toggle" id="btn-chuveiro" onclick="toggleRelay('chuveiro')">
                        Alternar
                    </button>
                </div>
                
                <div class="relay-card" id="card-ar">
                    <div class="relay-header">
                        <span class="relay-title">❄️ Ar Condicionado</span>
                        <span class="relay-status" id="status-ar">--</span>
                    </div>
                    <div class="relay-info">
                        Limite: 150.0 kWh
                    </div>
                    <div class="progress-bar">
                        <div class="progress-fill progress-ar" id="progress-ar" style="width: 0%"></div>
                    </div>
                    <div class="relay-info" id="info-ar">--</div>
                    <button class="btn-toggle" id="btn-ar" onclick="toggleRelay('ar')">
                        Alternar
                    </button>
                </div>
            </div>
        </div>
        
        <div class="controls">
            <button class="btn-reset" onclick="resetEnergy()">🔄 Reset Energia</button>
        </div>
        
        <div class="status">
            Última atualização: <span id="lastUpdate">--</span>
        </div>
    </div>

    <script>
        function updateData() {
            fetch('/data')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('voltage').textContent = data.voltage.toFixed(1);
                    document.getElementById('current').textContent = data.current.toFixed(2);
                    document.getElementById('power').textContent = data.power.toFixed(1);
                    document.getElementById('energy').textContent = data.energy.toFixed(3);
                    
                    updateRelayStatus('chuveiro', data.relayChuveiro, data.energy, data.limiteChuveiro);
                    updateRelayStatus('ar', data.relayArCondicionado, data.energy, data.limiteArCondicionado);
                    
                    document.getElementById('lastUpdate').textContent = new Date().toLocaleTimeString();
                })
                .catch(error => {
                    console.error('Erro ao buscar dados:', error);
                });
        }
        
        function updateRelayStatus(device, isOn, currentEnergy, limit) {
            const card = document.getElementById(`card-${device}`);
            const status = document.getElementById(`status-${device}`);
            const progress = document.getElementById(`progress-${device}`);
            const info = document.getElementById(`info-${device}`);
            const btn = document.getElementById(`btn-${device}`);
            
            const percentage = Math.min((currentEnergy / limit) * 100, 100);
            progress.style.width = `${percentage}%`;
            
            const remaining = Math.max(limit - currentEnergy, 0);
            
            if (currentEnergy >= limit) {
                card.className = 'relay-card blocked';
                status.textContent = 'BLOQUEADO';
                status.className = 'relay-status status-blocked';
                info.textContent = `Limite atingido! Economia: ${(currentEnergy - limit).toFixed(1)} kWh`;
                btn.disabled = true;
            } else if (isOn) {
                card.className = 'relay-card on';
                status.textContent = 'LIGADO';
                status.className = 'relay-status status-on';
                info.textContent = `Restam: ${remaining.toFixed(1)} kWh`;
                btn.disabled = false;
            } else {
                card.className = 'relay-card off';
                status.textContent = 'DESLIGADO';
                status.className = 'relay-status status-off';
                info.textContent = `Restam: ${remaining.toFixed(1)} kWh`;
                btn.disabled = false;
            }
        }
        
        function toggleRelay(device) {
            fetch(`/${device}/toggle`)
                .then(response => response.text())
                .then(data => {
                    alert(data);
                    updateData();
                })
                .catch(error => {
                    alert('Erro ao controlar o relé');
                    console.error(error);
                });
        }
        
        function resetEnergy() {
            if(confirm('Tem certeza que deseja resetar o contador de energia? Todos os relés serão religados.')) {
                fetch('/reset')
                    .then(response => response.text())
                    .then(data => {
                        alert(data);
                        updateData();
                    });
            }
        }
        
        // Atualizar dados a cada 2 segundos
        setInterval(updateData, 2000);
        
        // Primeira atualização
        updateData();
    </script>
</body>
</html>
)rawliteral";
  
  return html;
}