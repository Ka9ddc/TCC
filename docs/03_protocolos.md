# Protocolos Utilizados

## Visão Geral

O projeto utiliza múltiplos protocolos de comunicação em diferentes camadas do sistema.

---

## 1. UART (Universal Asynchronous Receiver-Transmitter)

### Função

Comunicação física entre ESP32-S3 e ESP32-C6.

### Configuração

```c
const uart_config_t uart_config = {
    .baud_rate = 115200,
    .data_bits = UART_DATA_8_BITS,
    .parity = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_DEFAULT,
};
```

### Características

| Parâmetro | Valor |
|-----------|-------|
| Velocidade | 115200 baud |
| Bits de dados | 8 |
| Paridade | Nenhuma |
| Bits de parada | 1 |
| Controle de fluxo | Nenhum |
| Buffer de RX | 1024 bytes |

### Vantagens

- Simples de implementar
- Não requer componentes extras
- Suficiente para tráfego de automação
- Fácil depuração com terminal serial

### Limitações

- Distância máxima curta (~1 metro)
- Sem detecção de erros (implementada no protocolo de aplicação)
- Ponto-a-ponto (não suporta múltiplos dispositivos)

---

## 2. Protocolo de Aplicação (JSON sobre UART)

### Formato das Mensagens

Todas as mensagens são JSON válido terminado com newline (`\n`).

### Tipos de Mensagem

#### 2.1 Command (S3 → C6)

Enviado do S3 para o C6 para executar ações.

**Exemplo:**

```json
{
  "type": "command",
  "request_id": 42,
  "device": "luz_sala",
  "action": "on"
}
```

**Campos:**

| Campo | Tipo | Obrigatório | Descrição |
|-------|------|-------------|-----------|
| type | string | Sim | Tipo da mensagem (`command`) |
| request_id | number | Sim | Identificador da requisição para correlacionar resposta |
| device | string | Sim | ID lógico do dispositivo |
| action | string | Sim | Ação a executar (`on`, `off`) |

#### 2.2 Response (C6 → S3)

Resposta do C6 após processar comando.

**Exemplo (sucesso):**

```json
{
  "type": "response",
  "request_id": 42,
  "device": "luz_sala",
  "status": "success"
}
```

**Exemplo (erro):**

```json
{
  "type": "response",
  "request_id": 42,
  "device": "luz_sala",
  "status": "error",
  "error": "device_not_found"
}
```

**Campos:**

| Campo | Tipo | Obrigatório | Descrição |
|-------|------|-------------|-----------|
| type | string | Sim | Tipo da mensagem (`response`) |
| request_id | number | Sim | Identificador da requisição original |
| device | string | Sim | ID do dispositivo |
| status | string | Sim | `success` ou `error` |
| error | string | Não | Descrição do erro (se status=error) |

#### 2.3 Event (C6 → S3)

Notificação de evento do C6 para o S3.

**Exemplo:**

```json
{
  "type": "event",
  "device": "sensor_sala",
  "event": "motion",
  "value": "true"
}
```

**Campos:**

| Campo | Tipo | Obrigatório | Descrição |
|-------|------|-------------|-----------|
| type | string | Sim | Tipo da mensagem (`event`) |
| device | string | Sim | ID do dispositivo |
| event | string | Sim | Tipo de evento |
| value | string | Sim | Valor do evento |

#### 2.4 Device discovered (C6 → S3)

O C6 envia este evento quando encontra o cluster On/Off em um endpoint do
dispositivo pareado:

```json
{
  "type": "event",
  "device": "luz_sala",
  "event": "device_discovered",
  "value": "on_off",
  "short_address": 18457,
  "endpoint": 1,
  "ieee_address": "xx:xx:xx:xx:xx:xx:xx:xx"
}
```

O endereço IEEE identifica permanentemente o dispositivo. O endereço curto
pode mudar quando o dispositivo retorna à rede e deve ser atualizado pelo C6.
O S3 persiste esses dados no namespace NVS `devices`; após reinicialização, o
último cadastro conhecido pode aparecer como `online: false` até uma nova
descoberta pelo C6.

Quando uma atualização do atributo On/Off é recebida, o C6 envia:

```json
{
  "type": "event",
  "device": "luz_sala",
  "event": "state_changed",
  "value": true,
  "short_address": 18457,
  "endpoint": 1
}
```

O S3 atualiza o estado conhecido do dispositivo e o persiste no NVS.

### 2.5 Regras de automação

O S3 suporta até oito regras persistentes. Cada regra possui um identificador
numérico e pode ser cadastrada com:

```json
{
  "trigger_device": "luz_sala",
  "trigger_event": "state_changed",
  "trigger_value": true,
  "action_device": "luz_sala",
  "action": "off"
}
```

O retorno de `POST /api/rules` inclui o identificador:

```json
{
  "status": "rule_saved",
  "id": 1
}
```

As regras são persistidas no namespace NVS `automation` e executadas localmente
quando um evento compatível é recebido. Se mais de uma regra corresponder ao
mesmo evento, todas as ações são enviadas.

#### Endpoints de regras

```http
GET /api/rules
POST /api/rules
PUT /api/rules/{id}
DELETE /api/rules/{id}
```

`GET /api/rules` retorna uma lista. O `PUT` substitui a regra indicada e o
`DELETE` remove sua configuração persistida.

Para validação sem um sensor físico, o S3 disponibiliza temporariamente:

```http
POST /api/test/events
```

Exemplo:

```json
{
  "device": "luz_sala",
  "event": "state_changed",
  "value": true
}
```

Esse endpoint apenas injeta um evento no motor local e não representa um evento
recebido por Zigbee. Ele deve ser usado somente durante os testes do TCC.

### 2.6 Status e métricas

```http
GET /api/status
```

O endpoint retorna uptime, eventos recebidos, regras correspondentes, comandos
gerados pelo motor de automação, respostas UART com sucesso ou erro e a
quantidade de regras configuradas e habilitadas.

### 2.7 Histórico em memória

```http
GET /api/history
```

Retorna os últimos 16 eventos e comandos observados pelo motor de automação.
O histórico é mantido em RAM e é perdido quando o S3 reinicia; a persistência
de métricas e histórico será tratada em uma etapa posterior.

---

## 3. HTTP/REST

### Função

Interface de controle e monitoramento do hub.

### Endpoints Implementados

#### 3.1 GET /api/devices

Lista dispositivos cadastrados.

**Requisição:**

```http
GET /api/devices HTTP/1.1
Host: 192.168.4.1
```

**Resposta:**

```json
[
  {
    "id": "luz_sala",
    "type": "light",
    "online": true
  },
  {
    "id": "sensor_sala",
    "type": "motion",
    "online": true
  }
]
```

#### 3.2 POST /api/devices/{id}/command

Envia comando para dispositivo.

**Requisição:**

```http
POST /api/devices/luz_sala/command HTTP/1.1
Host: 192.168.4.1
Content-Type: application/json

{
  "action": "on"
}
```

**Resposta (sucesso):**

```json
{
  "status": "command_sent"
}
```

**Resposta (erro):**

```json
{
  "status": "error",
  "error": "device_not_found"
}
```

### Características

| Parâmetro | Valor |
|-----------|-------|
| Protocolo | HTTP/1.1 |
| Porta | 80 |
| Formato | JSON |
| Content-Type | application/json |

---

## 4. Zigbee

### Função

Comunicação sem fio com dispositivos de automação.

### Perfil Utilizado

- **Zigbee 3.0** (Home Automation)
- **ZCL (Zigbee Cluster Library)**

### Clusters Suportados

#### 4.1 On/Off Cluster (0x0006)

Controle de lâmpadas e relés.

**Comandos:**

- `ON` (0x01): Liga dispositivo
- `OFF` (0x00): Desliga dispositivo

**Exemplo de uso:**

```c
esp_zb_zcl_on_off_cmd_t cmd_req = {0};
cmd_req.zcl_basic_cmd.dst_addr_u.addr_short = 0xb5f5;
cmd_req.zcl_basic_cmd.dst_endpoint = 10;
cmd_req.zcl_basic_cmd.src_endpoint = 1;
cmd_req.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;
cmd_req.on_off_cmd_id = ESP_ZB_ZCL_CMD_ON_OFF_ON_ID;
esp_zb_zcl_on_off_cmd_req(&cmd_req);
```

#### 4.2 Basic Cluster (0x0000)

Informações do dispositivo.

**Atributos:**

- Manufacturer Name
- Model Identifier
- Power Source

#### 4.3 Identify Cluster (0x0003)

Identificação de dispositivos (piscar luz, etc.).

#### 4.4 Occupancy Sensing Cluster (0x0406)

Sensores de presença.

**Atributos:**

- Occupancy (0x0000): true/false

---

## 5. Wi-Fi

### Função

Conectividade de rede e acesso à interface web.

### Modos de Operação

#### 5.1 Access Point (AP)

Cria rede Wi-Fi própria do hub.

**Configuração:**

```c
#define EXAMPLE_ESP_WIFI_AP_SSID "MeuHub-IoT"
#define EXAMPLE_ESP_WIFI_AP_PASSWD "12345678"
#define EXAMPLE_ESP_WIFI_CHANNEL 1
#define EXAMPLE_MAX_STA_CONN 4
```

**Características:**

- SSID: `MeuHub-IoT`
- Senha: `12345678`
- Canal: 1
- Máx. conexões: 4
- Segurança: WPA2-PSK

#### 5.2 Station (STA)

Conecta à rede Wi-Fi doméstica.

**Configuração:**

```c
#define EXAMPLE_ESP_WIFI_STA_SSID "EQUIPE K"
#define EXAMPLE_ESP_WIFI_STA_PASSWD "12345678"
#define EXAMPLE_ESP_MAXIMUM_RETRY 5
```

**Características:**

- Reconecta automaticamente em caso de falha
- Timeout: 5 segundos
- Máx. tentativas: 5

---

## 6. NVS (Non-Volatile Storage)

### Função

Armazenamento persistente de configurações.

### Dados Armazenados

- Configurações Wi-Fi
- Dispositivos cadastrados
- Regras de automação
- Estado do sistema

### Exemplo de Uso

```c
nvs_handle_t nvs_handle;
ESP_ERROR_CHECK(nvs_open("tcc_hub", NVS_READWRITE, &nvs_handle));
ESP_ERROR_CHECK(nvs_set_str(nvs_handle, "device.luz_sala.type", "light"));
ESP_ERROR_CHECK(nvs_commit(nvs_handle));
```

---

## Comparação de Protocolos

| Protocolo | Camada | Velocidade | Confiabilidade | Uso |
|-----------|--------|------------|----------------|-----|
| UART | Física | 115200 baud | Alta | S3 ↔ C6 |
| HTTP | Aplicação | ~1 Mbps | Média | Interface usuário |
| Zigbee | Rede/Física | 250 kbps | Alta | Dispositivos |
| Wi-Fi | Rede/Física | ~50 Mbps | Média | Rede local |
| NVS | Armazenamento | - | Alta | Persistência |

---

## Segurança

### Estado Atual

- [ ] Autenticação HTTP
- [ ] Criptografia UART
- [ ] Zigbee Security (Install Codes)
- [ ] HTTPS/TLS

### Recomendações Futuras

1. Implementar autenticação básica no HTTP
2. Adicionar checksum nas mensagens UART
3. Habilitar segurança Zigbee
4. Usar WPA3 no Wi-Fi AP
