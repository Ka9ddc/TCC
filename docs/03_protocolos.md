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

## 1.1 Autenticação da API HTTP

As rotas que alteram o estado do hub ou expõem configurações exigem um token
Bearer no cabeçalho HTTP:

```http
Authorization: Bearer TCC-Hub-2026-Local
```

O firmware inicia com um token padrão definido em tempo de compilação. No
primeiro uso, ele pode ser substituído e persistido em NVS. A ausência do
cabeçalho ou um token incorreto resulta em `401 Unauthorized`. As rotas
públicas são `GET /api/devices` e `GET /api/status`, destinadas à descoberta e
ao diagnóstico local.

Rotas protegidas:

- `POST /api/devices/{id}/command`
- `DELETE /api/devices/{id}`
- `POST /api/zigbee/permit-join`
- `GET/POST/PUT/DELETE /api/rules`
- `POST /api/events`
- `POST /api/test/events`
- `GET /api/history`
- `POST /api/config/auth`
- `POST /api/config/wifi`

Para consultar a configuração não sensível e o estado atual da rede:

```http
GET /api/config
Authorization: Bearer <token>
```

Resposta:

```json
{
  "sta_ssid": "MinhaRede",
  "sta_connected": true,
  "ap_ssid": "MeuHub-IoT",
  "ap_connected_clients": 1,
  "ap_enabled": true,
  "restart_required": false
}
```

A resposta não inclui senhas, token ou outros segredos.

O endpoint `GET /api/status` também expõe as métricas de latência dos comandos
que receberam resposta do C6:

```json
{
  "command_latency_samples": 12,
  "command_latency_last_ms": 184,
  "command_latency_min_ms": 142,
  "command_latency_max_ms": 311,
  "command_latency_avg_ms": 201.5
}
```

O tempo é medido no S3 desde o envio do comando pela UART até a resposta
correspondente do C6. Timeouts continuam contabilizados separadamente em
`command_timeouts`.

Para trocar o token, envie o novo valor autenticado:

```http
POST /api/config/auth
Authorization: Bearer <token-atual>
Content-Type: application/json

{"token":"um-token-local-com-16-ou-mais"}
```

O token deve ter entre 16 e 63 caracteres imprimíveis, sem espaços. Após a
resposta `token_updated`, as próximas requisições devem usar o novo token.
**Dívida técnica:** ainda não há procedimento de recuperação caso o token seja
perdido; isso deve ser resolvido antes de uma versão distribuível.

### Configuração de Wi-Fi

As credenciais do Access Point e da estação podem ser persistidas pela rota
autenticada:

```http
POST /api/config/wifi
Authorization: Bearer <token>
Content-Type: application/json

{
  "sta_ssid": "MinhaRede",
  "sta_password": "senha-da-rede",
  "ap_ssid": "MeuHub-IoT",
  "ap_password": "senha-local"
}
```

O SSID da estação deve ter até 32 caracteres. O SSID do AP deve ter até 32
caracteres e sua senha deve ser vazia (AP aberto) ou ter entre 8 e 63
caracteres. A resposta indica `restart_required: true`; a configuração é
aplicada no próximo reinício para evitar interromper a requisição atual.

---

## 2. Protocolo de Aplicação (JSON sobre UART)

### Formato das Mensagens

Todas as mensagens são JSON válido terminado com newline (`\n`).
Todas as mensagens UART possuem `protocol_version: 1`. Mensagens com versão
ausente ou diferente são rejeitadas e contabilizadas como inválidas.

### Tipos de Mensagem

#### 2.1 Command (S3 → C6)

Enviado do S3 para o C6 para executar ações.

**Exemplo:**

```json
{
  "type": "command",
  "protocol_version": 1,
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
  "protocol_version": 1,
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

Códigos de erro padronizados incluem `invalid_command`,
`unsupported_protocol_version`, `device_not_found`, `unsupported_action`,
`invalid_duration`, `remove_failed` e `queue_full`.

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
| value | boolean/number | Sim | Valor do evento |

Dispositivos Wi-Fi enviam eventos pelo mesmo contrato para:

```http
POST /api/events
```

Exemplo de evento de luminosidade:

```json
{
  "device": "sensor_luminosidade",
  "event": "illuminance_changed",
  "value": 82.5,
  "unit": "lux"
}
```

O endpoint `/api/test/events` aceita o mesmo formato para simulações locais.

Ao receber o primeiro evento numérico Wi-Fi, o S3 cadastra o dispositivo em
`/api/devices` e persiste seu identificador, evento, unidade e último valor no
namespace NVS `wifi_devices`. Leituras numéricas com variação inferior a
`5.0` unidades ou recebidas em intervalo inferior a dois segundos são
consideradas ruído/debounce e não acionam regras. O sensor é marcado como
offline após 30 segundos sem eventos, mas volta a online quando uma nova
leitura é recebida.

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

O C6 mantém uma tabela para até oito dispositivos Zigbee. O primeiro dispositivo
descoberto mantém o ID compatível `luz_sala`; os dispositivos seguintes recebem
IDs provisórios no formato `zigbee_XX`. O endereço IEEE é usado para reconhecer
um dispositivo que voltou à rede, enquanto o endereço curto e o endpoint são
atualizados durante a nova descoberta.

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

O C6 também informa mudanças no ciclo de vida do dispositivo:

```json
{
  "type": "event",
  "device": "luz_sala",
  "event": "device_offline",
  "value": false,
  "short_address": 6140
}
```

Os eventos `device_online` e `device_offline` atualizam o status do registro
no S3. Quando a remoção solicitada é concluída, o C6 envia `device_removed` e
o S3 deixa de exibir o dispositivo em `GET /api/devices`.

O S3 mantém até oito registros Zigbee e correlaciona cada evento pelo ID
lógico enviado pelo C6. O primeiro dispositivo continua usando `luz_sala` e
os demais usam os IDs provisórios atribuídos pelo C6. Registros persistidos
antes da migração são carregados no novo formato automaticamente.

> **Dívida técnica:** o evento `device_offline` ainda depende de uma indicação
> explícita de saída da rede Zigbee. Um dispositivo desligado ou sem resposta
> pode continuar aparecendo como online até que o C6 implemente verificações
> periódicas de disponibilidade (por exemplo, leitura de atributo ou timeout
> de comunicação). Essa melhoria deve ser feita antes de considerar o status
> offline adequado para uso em regras críticas.

Para solicitar a remoção:

```http
DELETE /api/devices/luz_sala
```

A resposta HTTP aguarda a confirmação do C6:

```json
{
  "status": "success",
  "request_id": 51,
  "device": "luz_sala"
}
```

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

Para eventos numéricos, a regra pode usar limiares:

```json
{
  "trigger_device": "sensor_luminosidade",
  "trigger_event": "illuminance_changed",
  "trigger_value": 100,
  "operator": "less_than",
  "action_device": "luz_sala",
  "action": "on"
}
```

Os operadores disponíveis são `equals`, `not_equals`, `greater_than`,
`less_than`, `greater_or_equal` e `less_or_equal`. Regras booleanas existentes
continuam compatíveis e usam `equals` por padrão.

Uma regra pode executar até quatro ações em sequência:

```json
{
  "trigger_device": "sensor_luminosidade",
  "trigger_event": "illuminance_changed",
  "trigger_value": 100,
  "operator": "less_than",
  "actions": [
    {
      "device": "luz_sala",
      "action": "on"
    },
    {
      "device": "luz_corredor",
      "action": "on",
      "delay_seconds": 2
    }
  ]
}
```

Cada ação aceita `delay_seconds` antes do comando e
`duration_seconds` para enviar automaticamente a ação inversa depois da
duração. Por exemplo, `on` com `duration_seconds: 60` liga o dispositivo e
envia `off` após um minuto. As ações são processadas por uma fila FreeRTOS,
garantindo ordem e evitando escritas concorrentes na UART. Atrasos e durações
são executados por timers independentes, portanto uma ação temporizada não
bloqueia as ações seguintes. O formato antigo
com `action_device` e `action` continua aceito.

Os callbacks dos timers somente colocam ações prontas na fila. O envio UART,
o histórico e a criação de timers de duração são executados pela tarefa do
motor de automação, evitando sobrecarregar a tarefa interna `Tmr Svc` do
FreeRTOS.

Cada atraso ou duração cria um timer independente. A fila e a tarefa de
automação mantêm o envio UART fora do callback do timer. O cancelamento e a
deduplicação de timers ainda não fazem parte do contrato estável.

Condições compostas usando `and` ou `or` foram avaliadas em uma revisão
experimental, mas não fazem parte do firmware estável atual nem do aplicativo.
O contrato entregue mantém uma condição por regra:

```json
{
  "trigger_device": "sensor_luminosidade",
  "trigger_event": "illuminance_changed",
  "trigger_value": 100,
  "operator": "less_than",
  "actions": [
    {
      "device": "luz_sala",
      "action": "on"
    }
  ]
}
```

O S3 dispara somente quando a condição passa de falso para verdadeiro; quando
volta a falso, a regra fica armada para uma nova execução. O formato legado com
`action_device` e `action` continua aceito. Condições compostas ficam como
trabalho futuro, depois que o app tiver um editor e testes para esse contrato.

Uma regra pode restringir a execução a uma janela de horário local usando
`start_time` e `end_time` no formato `HH:MM`:

```json
{
  "trigger_device": "sensor_luminosidade",
  "trigger_event": "illuminance_changed",
  "trigger_value": 100,
  "operator": "less_than",
  "start_time": "08:00",
  "end_time": "18:00",
  "actions": [
    {
      "device": "luz_sala",
      "action": "on"
    }
  ]
}
```

Os limites são inclusivos. Se `start_time` for posterior a `end_time`, a
janela atravessa a meia-noite; por exemplo, `22:00`–`06:00`. A avaliação usa o
relógio local do S3. Até que o relógio seja sincronizado, regras com janela
permanecem bloqueadas. O estado da sincronização é exposto em `/api/status` e
`/api/config`.

### 2.6 Correlação de comandos

Todo comando enviado ao C6 recebe um `request_id`. O S3 mantém os comandos
recentemente enviados como pendentes e remove cada um somente quando recebe
uma resposta do C6 com o mesmo identificador:

```json
{
  "type": "response",
  "request_id": 42,
  "device": "luz_sala",
  "status": "success"
}
```

Comandos sem resposta após dez segundos são contabilizados como timeout.
`GET /api/status` expõe `pending_commands` e `command_timeouts`.
Também expõe `uart_invalid_messages`, `uart_unknown_responses` e
`uart_duplicate_responses`.

Para abrir a rede Zigbee para pareamento, o S3 envia ao C6:

```json
{
  "type": "command",
  "request_id": 50,
  "device": "zigbee",
  "action": "permit_join",
  "duration_seconds": 60
}
```

O campo `duration_seconds` aceita valores de 1 a 180. O mesmo comando pode
ser acionado pela API HTTP:

```http
POST /api/zigbee/permit-join
Content-Type: application/json

{"duration_seconds":60}
```

Essa rota aguarda a confirmação do C6 e retorna `success`, `error` ou
`timeout`, seguindo os mesmos códigos HTTP dos comandos manuais.

Comandos enviados por regras de automação são assíncronos: a regra dispara a
ação e o resultado fica disponível no histórico e nas métricas. Já uma
requisição manual em `POST /api/devices/{id}/command` aguarda a resposta
correlacionada do C6. O endpoint retorna `200 OK` para `success`, `502 Bad
Gateway` para uma resposta `error` do C6 e `504 Gateway Timeout` quando não
há resposta dentro do prazo.

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

### 2.7 Status e métricas

```http
GET /api/status
```

O endpoint retorna uptime, eventos recebidos, regras correspondentes, comandos
gerados pelo motor de automação, respostas UART com sucesso ou erro e a
quantidade de regras configuradas e habilitadas.

O histórico de dispositivos também pode ser consultado em
`GET /api/devices`. Para sensores Wi-Fi, a resposta inclui `protocol`, `value`,
`unit`, `online` e `last_seen_seconds`. Esse último campo representa a
quantidade de segundos transcorridos desde a última leitura aceita.

### 2.8 Histórico em memória

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
  "status": "success",
  "request_id": 42,
  "device": "luz_sala"
}
```

**Resposta (erro do C6):**

```json
{
  "status": "error",
  "request_id": 42,
  "device": "luz_sala"
}
```

**Resposta (timeout):**

```json
{
  "status": "timeout",
  "request_id": 42,
  "device": "luz_sala"
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

#### 5.3 Sincronização de horário

O contrato planejado para essa etapa usa `start_time` e `end_time` no formato
`HH:MM`, inclusive para intervalos que atravessam meia-noite. A revisão estável
atual do firmware ainda não expõe esses campos nem `time_synced`, `timezone` e
`local_time` no endpoint autenticado `GET /api/config`. O aplicativo mantém o
editor oculto até detectar explicitamente esses campos, evitando enviar uma
configuração que seria ignorada.

Quando a etapa for retomada, o S3 deverá sincronizar o horário via SNTP após
obter IP na rede STA e bloquear regras com janela até a sincronização. A
sincronização dependerá de conectividade do STA; o AP local continuará

### Dívida técnica: stack overflow em reboot com histórico em NVS

Durante a persistência do histórico em NVS, foi identificado um risco de corrupção
de memória que levou a reinicialização em loop do S3 após reboot. O problema foi
observado em cenários de persistência e carga do histórico e, por prudência, a
funcionalidade foi revertida para manter o sistema estável para a etapa atual.

**Impacto:** risco de crash do firmware após reinicialização e comportamento
instável em operação contínua.

**Status:** dívida técnica registrada; reestudo e correção planejados após a entrega
do MVP funcional com interface mínima e descoberta mDNS.

funcionando sem Internet.

---

## 6. NVS (Non-Volatile Storage)

### Função

Armazenamento persistente de configurações.

### Dados Armazenados

- Configurações Wi-Fi
- Dispositivos cadastrados
- Regras de automação
- Estado do sistema

O registro de dispositivos Zigbee é persistido nos dois controladores. O S3
guarda o cadastro exposto pela API, incluindo ID, endereço curto, endpoint,
IEEE e último estado conhecido. O C6 guarda a tabela necessária para
reconhecer os dispositivos após reinicialização e atualiza os endereços quando
eles anunciam novamente na rede Zigbee. Um dispositivo carregado do NVS começa
como offline até que o C6 confirme sua presença.

### Recuperação do token

Para evitar uma rota HTTP de recuperação sem autenticação, o token possui um
procedimento físico de restauração. Ligue o S3 normalmente, sem pressionar o
botão BOOT, e pressione-o durante os primeiros três segundos da aplicação.
O firmware detecta o GPIO0 em nível baixo, remove o token persistido e restaura.
Não mantenha GPIO0 pressionado durante o reset, pois isso pode selecionar o
bootloader do ESP32-S3 em algumas placas.

O token restaurado é:

```text
TCC-Hub-2026-Local
```

Após o boot, solte o botão e use o token padrão para configurar um novo token
em `POST /api/config/auth`. O endpoint `GET /api/config` informa
`token_recovered: true` somente no boot em que a recuperação ocorreu; nunca
retorna o token.

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

## Provisionamento BLE e acesso HTTP

O provisionamento inicial usa BLE apenas para transportar as credenciais da rede
doméstica. Depois que o S3 obtém um endereço IP, ele envia esse endereço no
estado `WIFI_CONNECTED`.

O aplicativo valida `GET /api/status` antes de salvar a conexão. Como o
firmware opera em AP+STA, o aplicativo tenta primeiro o IP informado pelo STA
e, se necessário, o endereço do AP local `192.168.4.1`. Se nenhum dos dois
responder, o aplicativo mantém a tela de provisionamento e orienta o usuário a
conectar o celular à mesma rede do hub.

Como a API local usa HTTP durante o protótipo, a build Android habilita
explicitamente tráfego cleartext para endereços locais. Essa configuração é
necessária para que o aplicativo consiga acessar `http://192.168.x.x`; ela não
substitui HTTPS para uma versão distribuível fora da rede local.
