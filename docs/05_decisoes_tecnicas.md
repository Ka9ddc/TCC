# Decisões Técnicas

## 1. Arquitetura com Dois ESP32

### Decisão

Utilizar dois microcontroladores: ESP32-S3 e ESP32-C6.

### Alternativas Consideradas

1. **Apenas ESP32-C6**:
   - ❌ Wi-Fi limitado
   - ❌ Menos memória
   - ✅ Mais barato

2. **Apenas ESP32-S3 + Módulo Zigbee externo**:
   - ✅ Mais simples
   - ❌ Custo maior (módulo extra)
   - ❌ Mais complexo (SPI/UART extra)

3. **ESP32 + ESP32-C6 como RCP**:
   - ✅ Separação clara
   - ❌ Custo maior
   - ✅ Escalável

### Justificativa

- **ESP32-C6**: Único ESP32 com rádio 802.15.4 nativo para Zigbee
- **ESP32-S3**: Melhor para Wi-Fi, mais memória, mais GPIOs
- **Separação de concerns**: Cada chip foca em uma tarefa
- **Custo-benefício**: Ambos são baratos (< R$ 100 total)

### Consequências

- ✅ Código modular
- ✅ Fácil depuração (dois logs separados)
- ✅ Escalável (pode adicionar mais bridges)
- ❌ Consumo maior (dois chips)
- ❌ Complexidade de comunicação (UART)

---

## 2. UART para Comunicação Interna

### Decisão

Utilizar UART (115200 baud) para comunicação entre S3 e C6.

### Alternativas Consideradas

1. **SPI**:
   - ✅ Mais rápido
   - ❌ Mais GPIOs (4 vs 2)
   - ❌ Mais complexo

2. **I2C**:
   - ✅ Menos GPIOs
   - ❌ Mais lento
   - ❌ Requer pull-ups

3. **ESP-NOW**:
   - ✅ Sem fios
   - ❌ Mais complexo
   - ❌ Conflito com Wi-Fi/Zigbee

4. **MQTT (via Wi-Fi)**:
   - ✅ Desacoplado
   - ❌ Requer broker
   - ❌ Mais lento

### Justificativa

- **Simplicidade**: 2 GPIOs, sem componentes extras
- **Confiabilidade**: Comunicação ponto-a-ponto direta
- **Velocidade adequada**: 115200 baud = ~11 KB/s (suficiente)
- **Depuração fácil**: Pode ler com qualquer terminal serial

### Consequências

- ✅ Fácil implementação
- ✅ Logs claros
- ❌ Distância máxima curta (~1m)
- ❌ Sem detecção de erros (precisa implementar no protocolo)

---

## 3. JSON como Formato de Mensagem

### Decisão

Utilizar JSON para mensagens UART.

### Alternativas Consideradas

1. **Binário estruturado**:
   - ✅ Mais eficiente
   - ❌ Difícil depuração
   - ❌ Menos flexível

2. **Texto simples (CSV)**:
   - ✅ Simples
   - ❌ Menos legível
   - ❌ Difícil estender

3. **Protocol Buffer**:
   - ✅ Eficiente
   - ❌ Complexo
   - ❌ Requer schema

### Justificativa

- **Legibilidade**: Fácil de entender em logs
- **Ecossistema**: cJSON já está no ESP-IDF
- **Flexibilidade**: Adicionar campos é trivial
- **Depuração**: Pode testar manualmente

### Consequências

- ✅ Fácil desenvolvimento
- ✅ Logs claros
- ❌ Overhead de parsing
- ❌ Mensagens maiores

---

## 4. Baseado em Exemplos Oficiais do ESP-IDF

### Decisão

Utilizar exemplos oficiais do ESP-IDF como base.

### Alternativas Consideradas

1. **Começar do zero**:
   - ✅ Controle total
   - ❌ Mais tempo
   - ❌ Mais bugs

2. **Usar frameworks de terceiros**:
   - ✅ Mais rápido
   - ❌ Dependência externa
   - ❌ Menos aprendizado

### Justificativa

- **Confiabilidade**: Exemplos testados pela Espressif
- **Documentação**: Bem documentados
- **Manutenção**: Fácil comparar com originais
- **Aprendizado**: Entende o ESP-IDF profundamente

### Consequências

- ✅ Menos bugs
- ✅ Fácil manter
- ✅ Bom para TCC
- ❌ Código menos "original"

---

## 5. Zigbee 3.0 Home Automation

### Decisão

Utilizar Zigbee 3.0 com perfil Home Automation.

### Alternativas Consideradas

1. **Zigbee Light Link (ZLL)**:
   - ✅ Focado em iluminação
   - ❌ Menos dispositivos
   - ❌ Obsoleto (integrado ao 3.0)

2. **Thread**:
   - ✅ Mais moderno
   - ❌ Menos dispositivos no mercado
   - ❌ Requer border router

3. **Z-Wave**:
   - ✅ Mais confiável
   - ❌ Chips mais caros
   - ❌ Ecossistema fechado

### Justificativa

- **Mercado**: Muitos dispositivos Zigbee disponíveis
- **Custo**: Chips ESP32-C6 são baratos
- **SDK**: ESP-Zigbee-SDK oficial da Espressif
- **Padrão**: Zigbee 3.0 é o mais atual

### Consequências

- ✅ Amplo suporte a dispositivos
- ✅ SDK oficial
- ❌ Complexidade do stack Zigbee
- ❌ Conflito com Wi-Fi (mesma frequência)

---

## 6. Operação Local (Sem Nuvem)

### Decisão

Todo processamento ocorre localmente, sem dependência de Internet.

### Alternativas Consideradas

1. **Nuvem (AWS IoT, Azure, etc.)**:
   - ✅ Escalável
   - ❌ Dependência de Internet
   - ❌ Privacidade
   - ❌ Custo recorrente

2. **Híbrido (local + nuvem)**:
   - ✅ Melhor dos dois mundos
   - ❌ Complexidade
   - ❌ Manutenção

### Justificativa

- **Privacidade**: Dados não saem da rede local
- **Confiabilidade**: Funciona sem Internet
- **Latência**: Processamento local é mais rápido
- **Custo**: Sem assinaturas ou APIs pagas

### Consequências

- ✅ Privacidade total
- ✅ Funciona offline
- ❌ Acesso remoto limitado
- ❌ Backup manual

---

## 7. HTTP REST para Interface

### Decisão

Expor API HTTP REST para controle.

### Alternativas Consideradas

1. **WebSocket**:
   - ✅ Bidirecional
   - ❌ Mais complexo
   - ❌ Overhead

2. **MQTT**:
   - ✅ Leve
   - ❌ Requer broker
   - ❌ Mais complexo

3. **gRPC**:
   - ✅ Eficiente
   - ❌ Complexo
   - ❌ Overkill

### Justificativa

- **Simplicidade**: HTTP é universal
- **Ferramentas**: curl, Postman, navegador
- **Ecossistema**: esp_http_server no ESP-IDF
- **Aprendizado**: HTTP é essencial para IoT

### Consequências

- ✅ Fácil testar
- ✅ Amplamente conhecido
- ❌ Sem push (precisa polling)
- ❌ Overhead HTTP

---

## 8. NVS para Persistência

### Decisão

Utilizar NVS (Non-Volatile Storage) do ESP-IDF.

### Alternativas Consideradas

1. **SPIFFS/LittleFS**:
   - ✅ Arquivos
   - ❌ Mais complexo
   - ❌ Overhead

2. **EEPROM externa**:
   - ✅ Mais espaço
   - ❌ Componente extra
   - ❌ Mais GPIOs

3. **SD Card**:
   - ✅ Muito espaço
   - ❌ Componente extra
   - ❌ Lento

### Justificativa

- **Integrado**: Já está no ESP-IDF
- **Simples**: API key-value
- **Confiável**: Wear leveling incluso
- **Suficiente**: ~20 KB para configurações

### Consequências

- ✅ Sem componentes extras
- ✅ Fácil usar
- ❌ Espaço limitado
- ❌ Wear out (mas gerenciado)

---

## 9. GPIOs para UART

### Decisão

- S3: TX=GPIO17, RX=GPIO18
- C6: TX=GPIO4, RX=GPIO5

### Alternativas Consideradas

Qualquer par de GPIOs poderia ser usado.

### Justificativa

- **Disponibilidade**: Não conflitam com Wi-Fi, Zigbee, flash
- **Documentação**: GPIOs recomendados para UART no datasheet
- **Consistência**: Mesmos GPIOs em ambos os chips (facilita)

### Consequências

- ✅ Sem conflitos
- ✅ Fácil lembrar
- ❌ Limita outros usos desses GPIOs

---

## 10. Não Implementar Segurança Inicialmente

### Decisão

Deixar segurança (autenticação, criptografia) para trabalhos futuros.

### Alternativas Consideradas

1. **Implementar tudo agora**:
   - ✅ Seguro
   - ❌ Complexidade
   - ❌ Tempo

2. **Segurança parcial**:
   - ✅ Meio-termo
   - ❌ Falsa sensação
   - ❌ Complexidade

### Justificativa

- **Escopo**: Foco em funcionalidade primeiro
- **Tempo**: 2 meses é pouco para segurança robusta
- **Ambiente**: Rede local, não exposto à Internet
- **TCC**: Funcionalidade é prioridade

### Consequências

- ✅ Entrega no prazo
- ✅ Funcional
- ❌ Inseguro (mas documentado)
- ❌ Trabalho futuro necessário

## 11. Motor de Regras Local

### Decisão

Manter o motor de regras no ESP32-S3, com regras persistidas em NVS e
acionamento das saídas Zigbee por meio da fila UART para o ESP32-C6.

### Contrato atual

- Até 8 regras persistidas.
- Uma condição por regra, com operadores booleanos e numéricos.
- Até 4 ações por regra.
- Atraso e duração por ação, limitados a 24 horas.
- Janela opcional `start_time`/`end_time` no formato `HH:MM`.
- Eventos de teste em `POST /api/test/events`.
- Regras protegidas por autenticação nas rotas REST.

O aplicativo `meuhub` edita exatamente esse contrato. Condições compostas não
fazem parte da versão atual porque não há editor nem payload correspondente no
app.

### Segurança de atualização

O firmware valida uma regra inteira em uma estrutura temporária antes de
substituir a regra ativa e persistir em NVS. Assim, um JSON inválido não
corrompe parcialmente uma regra já configurada. Regras com janela dependem da
sincronização SNTP; sem relógio válido, a condição não dispara.

---

## Lições Aprendidas

1. **Comece simples**: UART + JSON foi mais fácil que esperado
2. **Exemplos oficiais**: Economizam muito tempo
3. **Dois chips**: Mais complexo que um, mas vale a pena
4. **Zigbee**: Stack complexo, mas SDK ajuda
5. **Documentação**: Escrever enquanto desenvolve facilita

---

## Decisões Pendentes

- [ ] Implementar autenticação HTTP?
- [ ] Adicionar checksum nas mensagens UART?
- [ ] Usar WPA3 no Wi-Fi AP?
- [ ] Implementar OTA updates?
- [ ] Adicionar mais clusters Zigbee?
