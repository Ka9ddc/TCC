# Hub de Automação Residencial com ESP32

## Visão Geral do Projeto

### Contexto

Este projeto de Trabalho de Conclusão de Curso (TCC) tem como objetivo desenvolver um hub/bridge de automação residencial baseado em microcontroladores ESP32, capaz de integrar dispositivos inteligentes utilizando diferentes protocolos de comunicação, com foco principal em Zigbee.

### Curso

Tecnólogo em Automação Industrial

### Período de Desenvolvimento

Setembro - Outubro de 2026 (2 meses)

---

## Problema

O mercado de automação residencial possui diversos hubs comerciais (Echo Dot, Hubitat, SmartThings, etc.), porém:

1. **Dependência de nuvem**: Muitos hubs requerem conexão com servidores externos para funcionar
2. **Custo elevado**: Soluções comerciais possuem preço alto para o usuário final
3. **Ecossistemas fechados**: Limitação na integração com dispositivos de diferentes fabricantes
4. **Privacidade**: Dados dos usuários são enviados para servidores de terceiros

---

## Solução Proposta

Desenvolver um hub de automação residencial com as seguintes características:

- **Operação local**: Funciona sem dependência de Internet
- **Baixo custo**: Utiliza componentes de baixo custo (ESP32-S3 e ESP32-C6)
- **Arquitetura modular**: Separação entre lógica de automação e comunicação
- **Protocolo aberto**: Comunicação via JSON sobre UART
- **Integração Zigbee**: Suporte a dispositivos Zigbee (lâmpadas, sensores, interruptores)

---

## Objetivos

### Objetivo Geral

Desenvolver um hub/bridge de automação residencial baseado em ESP32 com capacidade de:
- Gerenciar dispositivos Zigbee
- Executar regras de automação localmente
- Fornecer interface de acesso via HTTP
- Operar sem dependência de nuvem

### Objetivos Específicos

1. Implementar comunicação UART entre dois ESP32 (S3 e C6)
2. Desenvolver protocolo JSON para troca de mensagens
3. Criar servidor HTTP com API REST para controle de dispositivos
4. Implementar Zigbee Coordinator no ESP32-C6
5. Desenvolver motor de regras de automação no ESP32-S3
6. Validar funcionamento com dispositivos reais (lâmpadas, sensores)

---

## Componentes do Sistema

### Hardware

| Componente | Função |
|------------|--------|
| ESP32-S3 | Cérebro do sistema: Wi-Fi, HTTP, regras de automação |
| ESP32-C6 | Bridge de comunicação: Zigbee Coordinator |
| Dispositivos Zigbee | Lâmpadas, sensores, interruptores inteligentes |

### Software

| Módulo | Responsabilidade |
|--------|-----------------|
| Wi-Fi Manager | Gerencia conexões AP e STA |
| HTTP Server | API REST para controle |
| UART Protocol | Comunicação entre S3 e C6 |
| Zigbee App | Coordinator Zigbee |
| Device Registry | Mapeamento de dispositivos |
| Storage | Persistência em NVS |

---

## Funcionalidades

### Implementadas

- [x] Wi-Fi AP+STA (acesso local e conexão à rede doméstica)
- [x] UART bidirecional entre S3 e C6
- [x] Protocolo JSON sobre UART
- [x] HTTP server com endpoints básicos
- [x] Zigbee Coordinator
- [x] Comandos On/Off para dispositivos Zigbee

### Delimitação de escopo

- O MVP utiliza Zigbee e Wi-Fi local por HTTP/eventos.
- Protocolos proprietários Tuya/Elgin não fazem parte da implementação.
- Matter foi mantido como possibilidade de evolução, não como requisito do MVP.

---

## Diferenciais

1. **Operação offline**: Regras de automação executam localmente
2. **Arquitetura distribuída**: S3 pensa, C6 comunica
3. **Protocolo documentado**: JSON sobre UART facilita depuração
4. **Baixo custo**: Componentes acessíveis (ESP32)
5. **Código aberto**: Baseado em exemplos oficiais do ESP-IDF

---

## Estado Atual

O projeto está em fase de consolidação, com:

- ✅ Comunicação UART bidirecional entre S3 e C6
- ✅ Wi-Fi AP+STA e provisionamento BLE do S3
- ✅ HTTP server com API local autenticada
- ✅ Zigbee Coordinator e comandos On/Off
- ✅ Motor de regras e temporizadores no S3
- ✅ Persistência de configurações, regras e dispositivos em NVS
- ✅ Métricas de latência e contadores operacionais

---

## Próximos Passos

1. Executar testes finais de usabilidade e recuperação após reinicialização
2. Medir latência e taxa de sucesso em uma amostra de comandos
3. Testar regras com dispositivos Zigbee reais
4. Consolidar resultados e limitações no TCC

---

## Tecnologias Utilizadas

- **ESP-IDF v5.5.5**: Framework oficial da Espressif
- **ESP-Zigbee-SDK**: SDK oficial para Zigbee
- **cJSON**: Biblioteca para parsing JSON
- **FreeRTOS**: Sistema operacional em tempo real
- **Zigbee 3.0**: Protocolo de comunicação sem fio

---

## Repositório

O código fonte está organizado em dois projetos ESP-IDF:

- `hub_s3/`: Código do ESP32-S3
- `hub_c6/`: Código do ESP32-C6

Baseados nos exemplos oficiais:
- `examples/wifi/softap_sta` (hub_s3)
- `examples/zigbee/esp_zigbee_gateway` (hub_c6)
