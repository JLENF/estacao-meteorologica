/*
**********************************************************************
*   Estação meteorológica auto sustentável em código aberto          *
*   Projeto desenvolvido no TCC de Engenharia Elétrica - FURB        *
*   Para obtenção do título de Engenheiro Eletricista                *
*   Data: 12/03/2017                                                 *
*   Ultima alteração: 22/07/2017                                     *
*   Changelog:                                                       *
*   - incluido biblioteca wifimanager                                *
*   - incluído thingspeak                                            *
*   - incluído função para mostrar a data e hora atual (/hora)       *
*   - incluído função para mostrar temp. e umid. máximas (/max)      *
*   - incluído função para mostrar pluviometro por uma hora (/chuva) *
*                                                                    * 
*   Este projeto é de código aberto e utiliza partes de código       *
*   de outros projetos de código aberto:                             *
*   - http://cta.if.ufrgs.br                                         *
*   - http://air.imag.fr/mediawiki/index.php/SEN-08942               *
*   - https://github.com/PaulStoffregen/Time                         *
*   - https://github.com/esp8266/Arduino/issues/313                  *
*                                                                    *
*   Bibliotecas necessárias:                                         *
*   - https://github.com/arduino-libraries/NTPClient                 *
*   - https://github.com/PaulStoffregen/Time                         *
*   - https://github.com/adafruit/DHT-sensor-library                 *
*   - https://github.com/Gianbacchio/ESP8266-TelegramBot             *
*   - https://github.com/tzapu/WiFiManager                           *
*                                                                    *
*   Para compilar o ESP8266 no Arduino, cole a URL abaixo            *                                                    
*   em Arquivo -> Preferências -> URL:                               *
*   http://arduino.esp8266.com/stable/package_esp8266com_index.json  *
*                                                                    *
*   Você pode contribuir com este projeto e acompanhar seu           *
*   desenvolvimento no endereço oficial do projeto:                  *
*   http://www.github.com/jlenf/                                     *
*   Autor: Jairo Lenfers - jairolenfers@gmail.com                    *
*                                                                    *
**********************************************************************
*/

// Bibliotecas externas
#include <FS.h>                   //esta biclioteca é necessaria para envitar bugs
#include <WiFiManager.h>  // biblioteca para facilitar o gerenciamento wifi
#include <Ticker.h> // para o status do led
#include <ESP8266WiFi.h>
#include <ESP8266TelegramBOT.h>
#include <DHT.h>
#include <NTPClient.h>
#include <TimeLib.h>

// Variaveis adicionadas em 22/07/17
Ticker ticker;
#define TRIGGER_PIN 10 // pino que vai resetar a configuração - 10 aqui é o pino SD3 no nodemcu
unsigned long time_a = 0; // utilizado para funcao resetar com botao pressionado
const long time_i = 5000; // 5000 = 5 segundos

// Definição de constantes de calibração
#define CTE_CAL_ANEMOMETRO 0.9011     // 1 rev/segundo  = 0.9011 kph
#define CTE_CAL_PLUVIOMETRO 0.2794    // 1 batida = 0.2794 mm
#define DHTTYPE DHT22                 // Modelo do sensor de temperatura

// Definição de constantes do Telegram BOT 
// Utilize o BOT Father para criar seu token: https://core.telegram.org/bots
#define BOTtoken "123456789:xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"  // Token do BOT
#define BOTname "xxxxx"
#define BOTusername "xxxxx_bot"
String numero_tel = "12345678"; // Código telefone Jairo para teste

// Thingspeak
const int channelID = 123456;
String writeAPIKey = "xxxxxxxxxxxxxxxxxxx"; // write API key for your ThingSpeak Channel
const char* server = "api.thingspeak.com";
WiFiClient client;
unsigned long proximoThingSpeak = 0;
#define PERIODO_THINGSPEAK  20000 // 20 segundos

// Período entre as medidas em milisegundos
#define PERIODO_ANEMOMETRO  5000
#define PERIODO_DIR_VENTO   5000
#define PERIODO_PLUVIOMETRO  5000

// Pinos para conexão com o ESP8266
#define ANEMOMETRO_PIN   5     // Digital D1 Nodemcu
#define PLUVIOMETRO_PIN  4     // Digital D2 Nodemcu
#define DIR_VENTO_PIN    A0    // Analog A0 Nodemcu
#define DHTPIN 2               // Digital D4 Nodemcu

// Variáveis globais
float tempf ,humf, temp_maxf, hum_maxf;
String temp, hum, temp_max, hum_max, data_temp_max, data_hum_max;
int16_t utc = -3; //UTC -3:00 Brasil
String uptime_data;
String uptime_hora;
String atual_data;
String atual_hora;
String clientMac = "";
unsigned char mac[6];
int Bot_mtbs = 1000; 
long Bot_lasttime;   
bool Start = false;
const long intervalo_alerta = 60000; // = 60 segundos
double volume_minutos[60]; // utilizado para calcular o volume pluviometrico dos ultimos 60 minutos
int ult_minuto = 0; // utilizado na funcao para dizer qual o ultimo minuto antes de mudar de estado
double volume_hora_mm;

// Variáveis para incrementação
volatile int numRevsAnemometro = 0;
volatile int numBatidasBascula = 0;

// Variáveis para realização do polling
unsigned long proximaMedidaAnemometro = 0;
unsigned long proximaMedidaPluviometro = 0;
unsigned long proximaMedidaDirVento = 0;
unsigned long tempo = 0;
unsigned long previousMillis = 0;        // diferenca entre tempo de aquirição da temperatura
const long interval = 2000;              // verificar se precisa

// Variáveis para converter para String para mostrar no Telegram
String S_vel_med;  
String S_local_ip;
String S_ssid;
String S_direcao;
String S_volume_mm;
String S_volume_hora_mm;
String S_bot_name;
String S_bot_username;

// Inicialização gerais
DHT dht(DHTPIN, DHTTYPE, 11); // 11 works fine for ESP8266
TelegramBOT bot(BOTtoken, BOTname, BOTusername);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "a.st1.ntp.br", utc*3600, 60000);

int valor_adc;

void setup() {
  // -----------------------------------  
  // Biblioteca do Wifi (WiFiManager) - adicionado em 22/07/17
  WiFiManager wifiManager;

  // seta o led interno do Nodemcu como saída
  pinMode(BUILTIN_LED, OUTPUT);
  // inicia a funcao de piscar o led com 0.5 para informar que esta tentando conectar no wifi
  ticker.attach(0.6, tick);

  //seta o botao de reset como entrada
  pinMode(TRIGGER_PIN, INPUT);

  //sai da configuracao do WiFi apos conectado
  wifiManager.setBreakAfterConfig(true);

  // tenta conectar no WiFi com as ultimas configuracoes salvas
  // se ele nao conseguir conectar no WiFi, abre um ponto de acesso com nome variável (do proprio ESP)
  // se quiser colocar um nome específico, esta comentado do lado o SSID e password
  if (!wifiManager.autoConnect()) { //"SSID", "password")) { // se estiver em branco, cria uma rede com nome do ESP
    Serial.println("Falha ao conectar, tentando resetar o ESP para ver se conecta");
    delay(3000);
    ESP.reset();
    delay(5000);
  }

  // se aparecer esta mensagem na serial, conectou com sucesso no WiFi
  Serial.println("Conectado no WiFi com sucesso!");
  ticker.detach();
  digitalWrite(BUILTIN_LED, LOW); // deixa o led ligado informando que conectou no wifi
  Serial.print("ip local: ");
  Serial.println(WiFi.localIP());

  // -----------------------------------
   Serial.begin(115200);
   dht.begin();           // inicia o sensor de temperatura


   pinMode(ANEMOMETRO_PIN, INPUT_PULLUP);
   pinMode(PLUVIOMETRO_PIN, INPUT_PULLUP);
   attachInterrupt(digitalPinToInterrupt(ANEMOMETRO_PIN), contadorAnemometro, FALLING);
   attachInterrupt(digitalPinToInterrupt(PLUVIOMETRO_PIN), contadorPluviometro, FALLING);

   WiFi.macAddress(mac);    // MAC Address que adquiriu
   clientMac += macToStr(mac);


   bot.begin(); // Inicia o bot telegram
   Serial.println("BOT started");
   pinMode(2, OUTPUT); // led do wifi conectado
   bot.sendMessage(numero_tel,"Estação 01 - Conectada!",""); // envia mensagem para numero padrao dizendo que ligou

   // Inicia NTP
   delay(2000);
   timeClient.begin();
   timeClient.update();
   setSyncProvider(&ntpSyncProvider);

   // Pega a data e hora que ligou o sistema
   delay(2000);
   uptime_data_hora();
 
   // Converte as variaveis para String para mostrar no Telegram
   S_local_ip = WiFi.localIP().toString();
   S_ssid = wifiManager.getSSID();
//   S_ssid = String(ssid);
   S_bot_name = String(BOTname);
   S_bot_username = String(BOTusername);
   S_bot_name.concat(" (");
   S_bot_name.concat(S_bot_username);
   S_bot_name.concat(")");
}

void loop() { // Loop principal
// adicionado em 22/07/17 - funcao para resetar as config do wifi
  // se o botao do reset nao estiver pressionado, atualiza a variavel do tempo
 if ( digitalRead(TRIGGER_PIN) == HIGH ) { 
   time_a = millis();
 }
 
  //  se o botao D4 (saida 2) for pressionado, comeca a contar o tempo dele pressionado
  if ( digitalRead(TRIGGER_PIN) == LOW ) {
    unsigned long time_b = millis();
    if(time_b - time_a >= time_i) {  // se o tempo for maior que o especificado (5 seg padrao), ele reseta as configuracoes
      time_a = time_b; 
      ticker.attach(0.1, tick);
      WiFiManager wifiManager2;
      wifiManager2.resetSettings();  // funcao para resetar as configuracoes
      delay(1500);
      //ESP.reset();  // foi efetuado um teste com esta funcao no final, mas nao precisa
    }
  }
// --------------------------------------------
  

   // Verifica se tem requisição do telegram
   if (millis() > Bot_lasttime + Bot_mtbs)  {
      bot.getUpdates(bot.message[0][1]);   // Chama a API GetUpdates
      Bot_ExecMessages();   // Responde as mensagens com ECHO
      Bot_lasttime = millis();
  }
  
    // Realizando o polling
   tempo = millis();

   if (tempo >= proximaMedidaAnemometro) {
      Serial.print("Vento (km/h): ");Serial.println(calcVelocidadeVento(), 2);
      proximaMedidaAnemometro = tempo + PERIODO_ANEMOMETRO;
   }
   if (tempo >= proximaMedidaDirVento) {
      //Serial.print("Direcao: ");Serial.println(calcDirecaoVento());
      calcDirecaoVento();
      proximaMedidaDirVento = tempo + PERIODO_DIR_VENTO;
   }
   if (tempo >= proximaMedidaPluviometro) {
      Serial.print("Chuva (mm): ");Serial.println(calcQuantidadeChuva(), 3);
      proximaMedidaPluviometro = tempo + PERIODO_PLUVIOMETRO;
   }
   if (tempo >= proximoThingSpeak) {
      publica_thingspeak();
      proximoThingSpeak = tempo + PERIODO_THINGSPEAK;
   }
   
   gettemperature(); // Fica atualizando a temperatura para variaveis globais
}

void tick()
{
  // funcao para piscar o led
  int state = digitalRead(BUILTIN_LED);  // obtem o status atual do pino GPIO1
  digitalWrite(BUILTIN_LED, !state);     // seta o oposto do status atual
}

void publica_thingspeak(){
    if (client.connect(server, 80)) {
    
    // Monta o corpo da API com os sensores
    String body = "field1=";
           body += S_vel_med;
           body += "&amp;field2=";
           body += String(valor_adc);
           body += "&amp;field3=";
           body += temp;
           body += "&amp;field4=";
           body += hum;
           body += "&amp;field5=";
           body += S_volume_mm;
    
    client.print("POST /update HTTP/1.1\n");
    client.print("Host: api.thingspeak.com\n");
    client.print("Connection: close\n");
    client.print("X-THINGSPEAKAPIKEY: " + writeAPIKey + "\n");
    client.print("Content-Type: application/x-www-form-urlencoded\n");
    client.print("Content-Length: ");
    client.print(body.length());
    client.print("\n\n");
    client.print(body);
    client.print("\n\n");
  }
  client.stop();
}

void uptime_data_hora() { // Funcao para pegar data e hora que o sistema ligou
   time_t t = now();
   uptime_hora = String(hour(t));
   uptime_hora.concat(":");
   uptime_hora.concat(minute(t));
   uptime_hora.concat(":");
   uptime_hora.concat(second(t));    
   uptime_data = String(day(t));
   uptime_data.concat("/");
   uptime_data.concat(month(t));
   uptime_data.concat("/");
   uptime_data.concat(year(t));
   uptime_data.concat(" ");
   uptime_data.concat(uptime_hora);
}

void atual_data_hora() { // Funcao para pegar a data e hora atual
   time_t a = now();
   atual_hora = String(hour(a));
   atual_hora.concat(":");
   atual_hora.concat(minute(a));
   atual_hora.concat(":");
   atual_hora.concat(second(a));    
   atual_data = String(day(a));
   atual_data.concat("/");
   atual_data.concat(month(a));
   atual_data.concat("/");
   atual_data.concat(year(a));
   atual_data.concat(" ");
   atual_data.concat(atual_hora);
}


String macToStr(const uint8_t* mac){ // Função para pegar o MAC Address e converter em String
   String result;
   for (int i = 0; i < 6; ++i) {
      result += String(mac[i], 16);
      if (i < 5)
      result += ':';
   }
   return result;
}

time_t ntpSyncProvider() { //  Função para o NTP
  return timeClient.getEpochTime();
}

void contadorAnemometro() { // Funções de callback de interrupção anemometro
   numRevsAnemometro++;
   Serial.println("============  INTERRUPCAO  ANEMOMETRO =================="); // DEBUG para serial quando recebeu interrupcao
}

void contadorPluviometro() { // Funções de callback de interrupção pluviometro
   numBatidasBascula++;
   Serial.println("============  INTERRUPCAO  PLUVIOMETRO =================="); // DEBUG para serial quando recebeu interrupcao
}

double calcVelocidadeVento(){ // Função para converter as aquisições com o periodo informado
   double velocidadeMedia;
   velocidadeMedia = numRevsAnemometro;
   velocidadeMedia *= 1000.0*CTE_CAL_ANEMOMETRO;
   velocidadeMedia /= PERIODO_ANEMOMETRO;
   // Resetando contador de pulsos do anemometro
   numRevsAnemometro = 0;
   S_vel_med =  String(velocidadeMedia);
   return velocidadeMedia;
}

void calcDirecaoVento() { // Função para transformar os valores analogicos em direções do vento
   int valor, x;
   valor = analogRead(DIR_VENTO_PIN);
   Serial.println(valor); // DEBUG CONVERSOR ANALOGICO SERIAL
  if (valor <= 61) {
    S_direcao = String("NO");
  } 
  else if (valor <= 68) { 
    S_direcao = String("O");
  } 
  else if (valor <= 77) { 
    S_direcao = String("SO");
  }
  else if (valor <= 91) { 
    S_direcao = String("S");
  }
  else if (valor <= 115) { 
    S_direcao = String("SE");
  }
  else if (valor <= 151) { 
    S_direcao = String("E");
  }
  else if (valor <= 207) {  
    S_direcao = String("NE");
  }
  else if (valor <= 335) {  
    S_direcao = String("N");
  }
  valor_adc = valor;
   Serial.println(S_direcao); 
   return;
   
}

void pluv_ultima_hora(double vol_5s){ // Funcao para incluir os valores do pluviometro na última hora
   time_t u = now();
   int now_min = minute(u);
   if (ult_minuto != now_min){
      volume_minutos[now_min] = 0; // inicia uma nova contagem no minuto atual quando ele chegar
      ult_minuto = now_min;
   }
   volume_minutos[now_min] += vol_5s;
}

void calc_pluv_ultima_hora(){ // Função para somar todos os valores da última hora do pluviometro
   int x;
   volume_hora_mm = 0;
   for (x = 0; x < 60; x++) {
      volume_hora_mm +=  volume_minutos[x];
      Serial.print(x);Serial.print(" ");Serial.println(volume_minutos[x]);  // DEBUG
   }
   S_volume_hora_mm = String(volume_hora_mm);
}

void zera_pluv_ultima_hora(){ // Função para zerar os valores do pluviometro da última hora
   int x;
   volume_hora_mm = 0;
   for (x = 0; x < 60; x++) {
      volume_minutos[x] = 0;
   }
   S_volume_hora_mm = String(volume_hora_mm);
}
double calcQuantidadeChuva(){ // Função para calcular a quantidade de chuva instantânea
    double volumeMedio;
    volumeMedio = numBatidasBascula;
    volumeMedio *= 1000.0*CTE_CAL_PLUVIOMETRO;
    volumeMedio /= PERIODO_PLUVIOMETRO;
    numBatidasBascula = 0;
    pluv_ultima_hora(volumeMedio); // envia para funcao para calcular a media da ultima hora
    S_volume_mm = String(volumeMedio); // transforma em String para funcionar no Telegram
    return volumeMedio;
}

void gettemperature() { // Função para pegar os dados de temperatura e umidade do sensor DHT22
   // Espera dois segundos entre as medicoes.
   // se a diferenca do tempo entre o tempo atual e o ultimo tempo que leu
   // o sensor for maior que o intervado setado, vai ler o sensor
   // Funciona melhor que delay 
   unsigned long currentMillis = millis();
   if(currentMillis - previousMillis >= interval) {
      // salva o ultimo tempo que leu o sensor
      previousMillis = currentMillis;
      humf = dht.readHumidity();          // Faz a leitura da Umidade (percentual)
      tempf = dht.readTemperature();     // Faz a leitura da Temperatura
      if (isnan(humf) || isnan(tempf)) { // Função para detectar se houve erro na leitura do sensor
         Serial.println("Falha ao ler o sensor DHT22!");
         return;
      }else{
         temp =  String(tempf);
         hum =  String(humf);
         if (tempf >= temp_maxf){ // Detecta se a temperatura é maior que a maxima registrada
           atual_data_hora(); // Se for, anota a data e hora que ocorreu
           data_temp_max = atual_data;
           temp_max = String(tempf);
           temp_maxf = tempf;
         }
         if (humf >= hum_maxf){ // Detecta se a umidade é maior que a maxima registrada
           atual_data_hora();  // Se for, anota a data e hora que ocorreu
           data_hum_max = atual_data;
           hum_max = String(humf);
           hum_maxf = humf;
         }
      }
   }
}

void Bot_ExecMessages() { // Função que processa as mensagens do telegram
  for (int i = 1; i < bot.message[0][0].toInt() + 1; i++)      {
    bot.message[i][5]=bot.message[i][5].substring(1,bot.message[i][5].length());
    Serial.println(bot.message[i][5]); // DEBUG temporario - para saber o que ele esta recebendo, para saber como tratar
    if (bot.message[i][5] == "status") {
      bot.sendMessage(bot.message[i][4], "Status: Conectado", "");
      bot.sendMessage(bot.message[i][4], "WiFi IP: " + S_local_ip, ""); 
      bot.sendMessage(bot.message[i][4], "MAC addr: " + clientMac, "");
      bot.sendMessage(bot.message[i][4], "SSID: " + S_ssid, "");
      bot.sendMessage(bot.message[i][4], "BOT: " + S_bot_name, "");
      bot.sendMessage(bot.message[i][4], "Online desde: " + uptime_data, "");
    }
    if (bot.message[i][5] == "vento") {  
      bot.sendMessage(bot.message[i][4], "Vento : " + S_vel_med + " (km/h)", ""); 
      bot.sendMessage(bot.message[i][4], "Direção: " + S_direcao, "");
    }
    if (bot.message[i][5] == "chuva") {
      calc_pluv_ultima_hora();
      bot.sendMessage(bot.message[i][4],"Chuva (instantânea): " + S_volume_mm + " (mm)","");
      bot.sendMessage(bot.message[i][4],"Chuva (ultima hora): " + S_volume_hora_mm + " (mm)","");
    }
    if (bot.message[i][5] == "zera") {
      zera_pluv_ultima_hora();
      calc_pluv_ultima_hora();
      bot.sendMessage(bot.message[i][4],"Informações da chuva na última hora zeradas!","");
      bot.sendMessage(bot.message[i][4],"Chuva (ultima hora): " + S_volume_hora_mm + " (mm)","");
    }
    if (bot.message[i][5] == "temp") {
      gettemperature();
      bot.sendMessage(bot.message[i][4],"Temperatura: " + temp + " *C","");
      bot.sendMessage(bot.message[i][4],"Umidade: " + hum + "%","");
    }
    if (bot.message[i][5] == "max") {
      gettemperature();
      bot.sendMessage(bot.message[i][4],"Temperatura (max): " + temp_max + " *C","");
      bot.sendMessage(bot.message[i][4],"Data: " + data_temp_max,"");
      bot.sendMessage(bot.message[i][4],"--------------------","");
      bot.sendMessage(bot.message[i][4],"Umidade (max): " + hum_max + "%","");
      bot.sendMessage(bot.message[i][4],"Data: " + data_hum_max,"");
    }
    if (bot.message[i][5] == "info") {
      bot.sendMessage(bot.message[i][4], "Vento : " + S_vel_med + " (km/h)", ""); 
      bot.sendMessage(bot.message[i][4], "Direção: " + S_direcao, "");
      bot.sendMessage(bot.message[i][4],"Chuva : " + S_volume_mm + " (mm)","");
      bot.sendMessage(bot.message[i][4],"Temperatura: " + temp + " *C","");
      bot.sendMessage(bot.message[i][4],"Umidade: " + hum + "%","");
    }
    if (bot.message[i][5] == "conf") {
      numero_tel = bot.message[i][4];
      bot.sendMessage(bot.message[i][4],"Configurado alerta para mostrar aqui","");
    }
    if (bot.message[i][5] == "hora") {
      atual_data_hora();
      bot.sendMessage(bot.message[i][4],"Data/Hora atual (ntp -3):","");
      bot.sendMessage(bot.message[i][4],atual_data,"");
    }
     if (bot.message[i][5] == "start") { // mensagem padrão quando adiciona o bot, mostrando as funções
      String wellcome = "Estação Meteorológica  WiFi";
      String wellcome1 = "/status : informa o status atual";
      String wellcome2 = "/vento : velocidade e direção do vento";
      String wellcome3 = "/chuva : milimetros que choveu última hora";
      String wellcome4 = "/zera : zerar informações da chuva na última hora";
      String wellcome5 = "/temp : temperatura e umidade";
      String wellcome6 = "/max : mostra temperatura e umidade maximas";
      String wellcome7 = "/info : mostra todas informações dos sensores";
      String wellcome8 = "/conf : configura onde vai mostrar os alertas";
      String wellcome9 = "/hora : mostra a hora atual do sistema (ntp)";
      bot.sendMessage(bot.message[i][4], wellcome, "");
      bot.sendMessage(bot.message[i][4], wellcome1, "");
      bot.sendMessage(bot.message[i][4], wellcome2, "");
      bot.sendMessage(bot.message[i][4], wellcome3, "");
      bot.sendMessage(bot.message[i][4], wellcome4, "");
      bot.sendMessage(bot.message[i][4], wellcome5, "");
      bot.sendMessage(bot.message[i][4], wellcome6, "");
      bot.sendMessage(bot.message[i][4], wellcome7, "");
      bot.sendMessage(bot.message[i][4], wellcome8, "");
      bot.sendMessage(bot.message[i][4], wellcome9, "");
      Start = true;
    }
    Serial.println(bot.message[i][4]);
  }
  bot.message[0][0] = "";   // todas mensagens foram respondidas
}
