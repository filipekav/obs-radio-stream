# OBS Radio Stream

**OBS Radio Stream** é um plugin customizado para o OBS Studio que permite capturar o mix de áudio master, codificá-lo em MP3 e transmiti-lo diretamente para servidores Icecast ou AzuraCast (Liquidsoap). Tudo isso através de uma interface nativa (Dock) integrada diretamente no OBS.

## ✨ Features

- **Streaming Direto de Áudio:** Transmite o áudio master do OBS para servidores Icecast ou AzuraCast.
- **Controle de Bitrate Customizável:** Codificação em MP3 utilizando `libmp3lame`.
- **Suporte a Autenticação (AzuraCast):** Conexão segura e autenticada com as credenciais do seu servidor.
- **Gravação Local de MP3:** Opção para gravar a transmissão de áudio localmente, enquanto envia para a rede.
- **Monitoramento em Tempo Real:** Inclui um timer (uptime) integrado na interface para acompanhar a duração da conexão ao vivo.

## 📥 Instalação (Para Usuários)

1. Vá até a aba [Releases](../../releases) no GitHub e baixe o arquivo `.zip` mais recente correspondente ao seu sistema operacional (Windows).
2. Extraia o conteúdo do `.zip` diretamente para o diretório de instalação do OBS Studio.
   - Normalmente localizado em: `C:\Program Files\obs-studio\`
3. Abra o OBS Studio.
4. Vá em **Docks (Docas)** no menu superior e ative o **OBS Radio Stream**.

## 🛠️ Instruções de Compilação (Para Desenvolvedores)

Para construir o plugin a partir do código-fonte, você precisará das seguintes dependências:

- **CMake** (3.28 ou superior)
- **Bibliotecas de Desenvolvimento (Dev Headers):**
  - `libmp3lame` (para codificação de MP3)

- **Qt6** (para construir a interface)

### Passo a passo:

1. Clone este repositório.
2. Configure o projeto utilizando o CMake. Certifique-se de que o CMake consegue encontrar a instalação do `libobs` (OBS Studio SDK), `Qt6` e `libmp3lame`.
3. Compile o projeto:
   ```bash
   cmake -B build
   cmake --build build --config Release
   ```
4. **Nota sobre as DLLs (Windows):** O script de build do CMake está configurado para copiar automaticamente as bibliotecas de tempo de execução (runtime `.dll`) do `libmp3lame` e `libshout` para a pasta de saída do projeto durante uma compilação bem-sucedida, facilitando o empacotamento.

## 🌐 Localization (Localização)

O plugin suporta os seguintes idiomas:

- **English** (en-US)
- **Portuguese** (pt-BR)
- **Spanish** (es-ES)
- **Russian** (ru-RU)
- **German** (de-DE)
- **French** (fr-FR)
- **Japanese** (ja-JP)
- **Chinese** (zh-CN)

> [!NOTE]
> Initial translations for languages other than Portuguese and English were generated with the help of AI. We are looking for native speakers to help refine and improve these translations.

If you'd like to contribute with a new language or improve an existing one, please feel free to submit a Pull Request with the updated `.ini` files in the `data/locale/` directory.

## 📄 Licença

Este projeto é licenciado sob a **GPLv2**. Veja o arquivo [LICENSE](LICENSE) para mais detalhes.
