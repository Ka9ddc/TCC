# Requisitos do Sistema

## Requisitos Funcionais

### RF01 - Gerenciar Conexão Wi-Fi AP

**Descrição:** O sistema deve criar uma rede Wi-Fi Access Point para acesso local.

**Critérios de Aceite:**
- SSID configurável (padrão: `MeuHub-IoT`)
- Senha configurável (padrão: `12345678`)
- Suportar até 4 conexões simultâneas
- Segurança WPA2-PSK

**Prioridade:** Alta

**Status:** ✅ Implementado

---

### RF02 - Gerenciar Conexão Wi-Fi STA

**Descrição:** O sistema deve conectar-se a uma rede Wi-Fi existente.

**Critérios de Aceite:**
- SSID e senha configuráveis
- Reconectar automaticamente em caso de falha
- Timeout de 5 segundos para conexão
- Máximo de 5 tentativas

**Prioridade:** Alta

**Status:** ✅ Implementado

---

### RF03 - Fornecer API HTTP REST

**Descrição:** O sistema deve expor endpoints HTTP para controle de dispositivos.

**Critérios de Aceite:**
- GET /api/devices: listar dispositivos
- POST /api/devices/{id}/command: enviar comando
- Respostas em formato JSON
- Content-Type: application/json

**Prioridade:** Alta

**Status:** ✅ Implementado (parcialmente)

---

### RF04 - Comunicar via UART

**Descrição:** O ESP32-S3 deve comunicar-se com o ESP32-C6 via UART.

**Critérios de Aceite:**
- UART_NUM_1
- Baud rate: 115200
- GPIOs: TX=17, RX=18 (S3) / TX=17, RX=16 (C6)
- Protocolo JSON terminado com newline

**Prioridade:** Alta

**Status:** ✅ Implementado

---

### RF05 - Controlar Dispositivos Zigbee

**Descrição:** O ESP32-C6 deve atuar como Zigbee Coordinator.

**Critérios de Aceite:**
- Formar rede Zigbee
- Comissionar dispositivos
- Enviar comandos On/Off
- Canal 13 (2.410 GHz)

**Prioridade:** Alta

**Status:** ✅ Implementado (parcialmente)

---

### RF06 - Executar Regras de Automação

**Descrição:** O sistema deve executar regras de automação localmente.

**Critérios de Aceite:**
- Regras do tipo EVENT → CONDITION → ACTION
- Execução local (sem nuvem)
- Suporte a múltiplas regras
- Persistência em NVS

**Prioridade:** Média

**Status:** ⏳ Em desenvolvimento

---

### RF07 - Persistir Configurações

**Descrição:** O sistema deve salvar configurações em memória não volátil.

**Critérios de Aceite:**
- Usar NVS do ESP-IDF
- Salvar configurações Wi-Fi
- Salvar dispositivos cadastrados
- Salvar regras de automação

**Prioridade:** Média

**Status:** ⏳ Em desenvolvimento

---

### RF08 - Notificar Eventos

**Descrição:** O sistema deve notificar eventos de dispositivos (ex.: sensor de presença).

**Critérios de Aceite:**
- Enviar evento C6 → S3 via UART
- Formato JSON
- Incluir device, event, value
- Processar no Rule Engine

**Prioridade:** Média

**Status:** ⏳ Em desenvolvimento

---

## Requisitos Não Funcionais

### RNF01 - Desempenho

**Latência:**
- Comando HTTP → Ação Zigbee: < 500 ms
- Evento Zigbee → Notificação: < 200 ms

**Throughput:**
- Mínimo de 10 comandos/segundo

**Status:** ⏳ A validar

---

### RNF02 - Confiabilidade

**Disponibilidade:**
- Uptime > 99% em operação contínua

**Taxa de Sucesso:**
- Comandos Zigbee: > 95%

**Recuperação:**
- Boot automático após falha de energia
- Reconexão Wi-Fi automática

**Status:** ⏳ A validar

---

### RNF03 - Usabilidade

**Interface:**
- API REST documentada
- Logs claros e informativos
- LEDs de status (opcional)

**Configuração:**
- SSID/senha editáveis no código
- Fácil flash via USB

**Status:** ✅ Implementado (parcialmente)

---

### RNF04 - Portabilidade

**Hardware:**
- ESP32-S3 e ESP32-C6
- Pinagem documentada

**Software:**
- ESP-IDF v5.5.5
- Baseado em exemplos oficiais

**Status:** ✅ Implementado

---

### RNF05 - Manutenibilidade

**Código:**
- Baseado em exemplos oficiais do ESP-IDF
- Comentários em português
- Estrutura modular

**Documentação:**
- README em cada projeto
- Este documento de requisitos

**Status:** ✅ Implementado

---

### RNF06 - Custo

**Hardware:**
- ESP32-S3: ~R$ 30-50
- ESP32-C6: ~R$ 25-40
- Total: < R$ 100

**Status:** ✅ Atendido

---

### RNF07 - Consumo Energético

**Alvo:**
- Standby: < 100 mA
- Ativo: < 300 mA

**Status:** ⏳ A validar

---

### RNF08 - Segurança

**Requisitos Futuros:**
- Autenticação HTTP
- Criptografia UART
- Zigbee Security
- WPA3 no Wi-Fi

**Status:** ❌ Não implementado

---

## Restrições

### Restrições Técnicas

1. **ESP-IDF v5.5.5**: Versão mínima requerida
2. **ESP-Zigbee-SDK**: Necessário para Zigbee no C6
3. **GPIOs limitados**: UART usa GPIOs específicos
4. **Memória**: NVS limitado (~20 KB)

### Restrições de Prazo

- **Desenvolvimento**: 2 meses (Set-Out 2026)
- **Entrega**: Outubro 2026
- **Apresentação**: Novembro 2026

### Restrições de Escopo

- Apenas dispositivos Zigbee On/Off
- Sem suporte a Matter inicialmente
- Sem aplicativo móvel
- Sem nuvem

---

## Matriz de Rastreabilidade

| ID | Descrição | Módulo | Status |
|----|-----------|--------|--------|
| RF01 | Wi-Fi AP | wifi_manager.c | ✅ |
| RF02 | Wi-Fi STA | wifi_manager.c | ✅ |
| RF03 | API HTTP | softap_sta.c | ✅ |
| RF04 | UART | softap_sta.c, esp_zigbee_gateway.c | ✅ |
| RF05 | Zigbee | esp_zigbee_gateway.c | ✅ |
| RF06 | Regras | (em desenvolvimento) | ⏳ |
| RF07 | NVS | softap_sta.c | ⏳ |
| RF08 | Eventos | (em desenvolvimento) | ⏳ |

---

## Critérios de Aceite do Projeto

Para aprovação do TCC, o sistema deve:

1. ✅ Ligar/desligar lâmpada Zigbee via HTTP
2. ✅ Receber evento de sensor Zigbee
3. ✅ Executar regra simples (ex.: sensor → luz)
4. ✅ Operar sem Internet
5. ✅ Documentar arquitetura e protocolos
6. ✅ Apresentar métricas de desempenho

---

## Trabalhos Futuros

- [ ] Suporte a mais clusters Zigbee (temperatura, umidade)
- [ ] Interface web completa
- [ ] Aplicativo móvel
- [ ] Integração com Home Assistant
- [ ] Suporte a Matter
- [ ] Segurança (TLS, autenticação)
