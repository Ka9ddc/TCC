# Arquitetura do Sistema

## Visão Geral

O sistema é composto por uma arquitetura distribuída com dois microcontroladores ESP32 comunicando-se via UART, onde cada dispositivo possui responsabilidades bem definidas.

---

## Diagrama de Blocos

```
                         USUÁRIO
                            │
                       Wi-Fi / HTTP
                            │
                            ▼
┌──────────────────────────────────────────────────┐
│                    ESP32-S3                      │
│                                                  │
│  ┌──────────────┐     ┌──────────────────────┐  │
│  │ Wi-Fi AP/STA │────►│ HTTP / REST API      │  │
│  └──────────────┘     └──────────┬───────────┘  │
│                                  │              │
│                                  ▼              │
│                       ┌─────────────────────┐   │
│                       │ UART Protocol       │   │
│                       └─────────┬───────────┘   │
│                                 │               │
│                       ┌─────────▼───────────┐   │
│                       │ UART Driver         │   │
│                       └─────────┬───────────┘   │
└─────────────────────────────────┼────────────────┘
                                  │ UART
                                  │ (115200 baud)
                                  │
┌─────────────────────────────────┼────────────────┐
│                                 ▼                │
│                    ESP32-C6                      │
│                                                  │
│  ┌───────────────────────────────────────────┐   │
│  │ UART Driver                               │   │
│  └──────────────────┬────────────────────────┘   │
│                     │                            │
│                     ▼                            │
│  ┌───────────────────────────────────────────┐   │
│  │ UART Protocol                             │   │
│  └──────────────────┬────────────────────────┘   │
│                     │                            │
│                     ▼                            │
│  ┌───────────────────────────────────────────┐   │
│  │ Zigbee Coordinator                        │   │
│  │ - Network Formation                       │   │
│  │ - Device Commissioning                    │   │
│  │ - ZCL Commands                            │   │
│  └──────────────────┬────────────────────────┘   │
│                     │                            │
└─────────────────────┼────────────────────────────┘
                      │
                   Zigbee
                      │
         ┌────────────┴────────────┐
         │                         │
       Sensor                   Atuador
    (movimento, temp)        (lâmpada, relay)
```

---

## Separação de Responsabilidades

### ESP32-S3 (Cérebro)

| Módulo | Descrição |
|--------|-----------|
| **Wi-Fi Manager** | Gerencia conexões Wi-Fi AP e STA |
| **HTTP Server** | API REST para controle de dispositivos |
| **UART Protocol** | Envia comandos e recebe eventos do C6 |
| **Storage (NVS)** | Persiste configurações e regras |
| **Rule Engine** | Executa regras de automação (em desenvolvimento) |

**Responsabilidades:**
- Interface com usuário (HTTP/Web)
- Lógica de automação
- Armazenamento de configurações
- Comunicação com C6 via UART

### ESP32-C6 (Bridge de Comunicação)

| Módulo | Descrição |
|--------|-----------|
| **Zigbee Coordinator** | Gerencia rede Zigbee |
| **UART Protocol** | Recebe comandos e envia eventos para S3 |
| **Device Registry** | Mapeia IDs lógicos para endereços Zigbee (em desenvolvimento) |

**Responsabilidades:**
- Formação e manutenção da rede Zigbee
- Comissionamento de dispositivos
- Tradução de comandos UART → Zigbee
- Notificação de eventos Zigbee → UART

---

## Comunicação UART

### Características Físicas

| Parâmetro | Valor |
|-----------|-------|
| Porta UART | UART_NUM_1 |
| Baud Rate | 115200 |
| Data Bits | 8 |
| Parity | None |
| Stop Bits | 1 |
| Flow Control | None |

### Pinagem

| ESP32-S3 | ESP32-C6 | Sinal |
|----------|----------|-------|
| GPIO 17 | GPIO 5 | TX → RX |
| GPIO 18 | GPIO 4 | RX ← TX |
| GND | GND | Terra comum |

### Formato da Mensagem

```
{JSON}\n
```

- Mensagens JSON terminadas com newline (`\n`)
- Fácil depuração via monitor serial
- Parsing simplificado com cJSON

---

## Fluxos de Dados

### Fluxo 1: Comando HTTP → Zigbee

```
1. Usuário → HTTP POST /api/devices/luz_sala/command
   Body: {"action": "on"}

2. HTTP Server (S3) → UART Protocol (S3)
   Envia comando para UART

3. UART (S3) → UART (C6)
   {"type":"command","device":"luz_sala","action":"on"}\n

4. UART Protocol (C6) → Zigbee Coordinator
   Parse JSON, identifica dispositivo

5. Zigbee Coordinator → Dispositivo
   esp_zb_zcl_on_off_cmd_req(ON)
```

### Fluxo 2: Evento Zigbee → HTTP

```
1. Sensor → Zigbee Coordinator (C6)
   Evento: occupancy = true

2. Zigbee Coordinator (C6) → UART Protocol (C6)
   Prepara evento

3. UART (C6) → UART (S3)
   {"type":"event","device":"sensor_sala","event":"motion","value":"true"}\n

4. UART Protocol (S3) → Rule Engine
   Processa evento

5. Rule Engine → HTTP Server
   Atualiza estado, notifica clientes
```

---

## Estrutura de Diretórios

### hub_s3

```
hub_s3/
├── CMakeLists.txt
├── README.md
└── main/
    ├── CMakeLists.txt
    └── softap_sta.c    # Código principal
```

### hub_c6

```
hub_c6/
├── CMakeLists.txt
├── README.md
└── main/
    ├── CMakeLists.txt
    └── esp_zigbee_gateway.c    # Código principal
```

---

## Considerações de Design

### Por que dois ESP32?

1. **ESP32-C6**: Possui rádio 802.15.4 nativo para Zigbee
2. **ESP32-S3**: Melhor para Wi-Fi e processamento HTTP
3. **Separação de concerns**: Cada chip foca em uma tarefa
4. **Escalabilidade**: Pode adicionar mais bridges no futuro

### Por que UART?

1. **Simplicidade**: Fácil de implementar e depurar
2. **Confiabilidade**: Comunicação ponto-a-ponto
3. **Baixo custo**: Não requer componentes extras
4. **Velocidade adequada**: 115200 baud é suficiente para automação

### Por que JSON?

1. **Legibilidade**: Fácil de entender em logs
2. **Flexibilidade**: Adicionar campos é trivial
3. **Ecossistema**: cJSON já está no ESP-IDF
4. **Depuração**: Pode testar com qualquer terminal serial

---

## Métricas de Desempenho (Alvo)

| Métrica | Valor Alvo |
|---------|------------|
| Latência comando → ação | < 500 ms |
| Taxa de sucesso de comandos | > 95% |
| Tempo de boot | < 5 s |
| Consumo em standby | < 100 mA |

---

## Trabalhos Futuros

- [ ] Adicionar suporte a Matter
- [ ] Implementar OTA (Over-The-Air updates)
- [ ] Adicionar mais protocolos (BLE, Wi-Fi direct)
- [ ] Criar aplicativo móvel
- [ ] Implementar segurança (TLS, autenticação)
