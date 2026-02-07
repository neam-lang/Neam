# Neam v0.6.3 OpenClaw Integration Guide

**Complete Documentation for Multi-Strategy Agent Integration**

This guide covers three complementary strategies for integrating Neam agents with external systems, enabling enterprise-grade AI agent deployments.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                     Neam v0.6.3 OpenClaw Integration                        │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌─────────────┐     ┌─────────────┐     ┌─────────────┐                   │
│  │  Strategy A │     │  Strategy B │     │  Strategy C │                   │
│  │  MCP Server │     │   Channel   │     │    A2A      │                   │
│  │             │     │   Gateway   │     │ Federation  │                   │
│  └──────┬──────┘     └──────┬──────┘     └──────┬──────┘                   │
│         │                   │                   │                           │
│         ▼                   ▼                   ▼                           │
│  ┌─────────────┐     ┌─────────────┐     ┌─────────────┐                   │
│  │   Claude    │     │  WhatsApp   │     │   Remote    │                   │
│  │   Desktop   │     │  Telegram   │     │   Agents    │                   │
│  │   Cursor    │     │   Slack     │     │   Cross-Org │                   │
│  └─────────────┘     └─────────────┘     └─────────────┘                   │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Table of Contents

1. [Overview](#overview)
2. [Strategy A: MCP Server](#strategy-a-mcp-server)
   - [Architecture](#a-architecture)
   - [Installation](#a-installation)
   - [Configuration](#a-configuration)
   - [Usage Examples](#a-usage-examples)
   - [Tool Reference](#a-tool-reference)
3. [Strategy B: Channel Gateway](#strategy-b-channel-gateway)
   - [Architecture](#b-architecture)
   - [Installation](#b-installation)
   - [Configuration](#b-configuration)
   - [Usage Examples](#b-usage-examples)
   - [Platform Integration](#b-platform-integration)
4. [Strategy C: A2A Federation](#strategy-c-a2a-federation)
   - [Architecture](#c-architecture)
   - [Installation](#c-installation)
   - [Configuration](#c-configuration)
   - [Usage Examples](#c-usage-examples)
   - [Trust Management](#c-trust-management)
5. [Combined Deployment](#combined-deployment)
6. [API Reference](#api-reference)
7. [Troubleshooting](#troubleshooting)

---

## Overview

Neam v0.6.3 introduces three complementary integration strategies:

| Strategy | Purpose | Best For |
|----------|---------|----------|
| **A: MCP Server** | Expose Neam agents as MCP tools | IDE integration, Claude Desktop, developer tools |
| **B: Channel Gateway** | Multi-channel messaging | Customer support, chatbots, WhatsApp/Telegram/Slack |
| **C: A2A Federation** | Distributed agent networks | Enterprise, cross-organization, microservices |

### When to Use Each Strategy

```
┌─────────────────────────────────────────────────────────────────┐
│                    Decision Tree                                 │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  Need IDE/Desktop integration?                                   │
│  ├─ YES → Strategy A (MCP Server)                               │
│  │                                                               │
│  Need multi-channel messaging (WhatsApp, Slack, etc.)?          │
│  ├─ YES → Strategy B (Channel Gateway)                          │
│  │                                                               │
│  Need cross-organization agent collaboration?                    │
│  ├─ YES → Strategy C (A2A Federation)                           │
│  │                                                               │
│  Building enterprise agent ecosystem?                            │
│  └─ YES → Combine all three strategies                          │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## Strategy A: MCP Server

### A: Architecture

The MCP (Model Context Protocol) Server exposes Neam agents and tools to MCP-compatible clients like Claude Desktop, Cursor, and other IDEs.

```
┌─────────────────────────────────────────────────────────────────┐
│                    MCP Server Architecture                       │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌──────────────┐         ┌──────────────┐                      │
│  │    Claude    │  stdio  │    Neam      │                      │
│  │   Desktop    │◄───────►│  MCP Server  │                      │
│  └──────────────┘         └───────┬──────┘                      │
│                                   │                              │
│  ┌──────────────┐                 │                              │
│  │    Cursor    │  stdio          │                              │
│  │     IDE      │◄────────────────┤                              │
│  └──────────────┘                 │                              │
│                                   ▼                              │
│                           ┌──────────────┐                      │
│                           │  Neam Agent  │                      │
│                           │   Runtime    │                      │
│                           └──────────────┘                      │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### A: Installation

#### Option 1: Pre-built Binary

```bash
# Download from releases
curl -L https://github.com/neam-lang/Neam/releases/download/v0.6.3/neam-mcp-server-macos -o neam-mcp-server
chmod +x neam-mcp-server

# Or on Linux
curl -L https://github.com/neam-lang/Neam/releases/download/v0.6.3/neam-mcp-server-linux -o neam-mcp-server
chmod +x neam-mcp-server
```

#### Option 2: Build from Source

```bash
git clone https://github.com/neam-lang/Neam.git
cd Neam
mkdir build && cd build
cmake .. && make neam-mcp-server
```

### A: Configuration

#### Claude Desktop Configuration

Add to `~/.config/claude/claude_desktop_config.json` (macOS/Linux) or `%APPDATA%\Claude\claude_desktop_config.json` (Windows):

```json
{
  "mcpServers": {
    "neam": {
      "command": "/path/to/neam-mcp-server",
      "args": ["--agent-dir", "/path/to/agents"],
      "env": {
        "OPENAI_API_KEY": "sk-...",
        "ANTHROPIC_API_KEY": "sk-ant-..."
      }
    }
  }
}
```

#### Cursor IDE Configuration

Add to `.cursor/mcp.json` in your project:

```json
{
  "servers": {
    "neam": {
      "command": "neam-mcp-server",
      "args": ["--project", "."],
      "capabilities": {
        "tools": true,
        "prompts": true,
        "resources": true
      }
    }
  }
}
```

#### Server Options

```bash
neam-mcp-server [OPTIONS]

Options:
  --agent-dir <PATH>      Directory containing .neam agent files
  --project <PATH>        Project directory with neam.toml
  --port <PORT>           HTTP port for SSE transport (default: stdio)
  --log-level <LEVEL>     Logging level: debug, info, warn, error
  --enable-voice          Enable voice tools (requires audio dependencies)
  --enable-knowledge      Enable RAG/knowledge tools
  --config <FILE>         Path to configuration file
```

### A: Usage Examples

#### Example 1: Basic Agent Tool

Create a simple agent in `agents/helper.neam`:

```neam
// Helper agent exposed via MCP
agent helper {
    name = "Code Helper"
    description = "Assists with code review and suggestions"
    model = "gpt-4"
    temperature = 0.3

    skill analyze_code {
        description = "Analyze code for improvements"
        parameters = {
            code: string,
            language: string
        }

        prompt = """
        Analyze the following {{language}} code and suggest improvements:

        ```{{language}}
        {{code}}
        ```

        Focus on:
        1. Performance optimizations
        2. Code clarity
        3. Best practices
        4. Potential bugs
        """
    }

    skill generate_tests {
        description = "Generate unit tests for code"
        parameters = {
            code: string,
            framework: string = "jest"
        }

        prompt = """
        Generate comprehensive unit tests for this code using {{framework}}:

        {{code}}
        """
    }
}
```

Start the server:

```bash
neam-mcp-server --agent-dir ./agents
```

In Claude Desktop, you can now use:

```
Use the analyze_code tool to review this Python function:

def fibonacci(n):
    if n <= 1:
        return n
    return fibonacci(n-1) + fibonacci(n-2)
```

#### Example 2: Knowledge-Enhanced Agent

```neam
// RAG-enabled documentation agent
knowledge docs_kb {
    source = "./documentation"
    chunk_size = 512
    chunk_overlap = 50
    embedding_model = "text-embedding-3-small"
}

agent docs_assistant {
    name = "Documentation Assistant"
    description = "Answers questions about project documentation"
    model = "gpt-4"
    knowledge = docs_kb

    skill search_docs {
        description = "Search documentation for information"
        parameters = {
            query: string,
            max_results: number = 5
        }
    }

    skill explain_concept {
        description = "Explain a concept from the documentation"
        parameters = {
            concept: string
        }

        prompt = """
        Using the documentation knowledge base, explain: {{concept}}

        Provide:
        1. Clear definition
        2. Usage examples
        3. Related concepts
        """
    }
}
```

#### Example 3: Multi-Tool Agent

```neam
// Agent with multiple specialized tools
agent dev_assistant {
    name = "Development Assistant"
    description = "Full-featured development helper"
    model = "claude-3-opus"

    // Code analysis
    skill review_pr {
        description = "Review a pull request"
        parameters = {
            diff: string,
            context: string = ""
        }
    }

    // Documentation
    skill generate_docs {
        description = "Generate documentation"
        parameters = {
            code: string,
            style: string = "jsdoc"
        }
    }

    // Refactoring
    skill suggest_refactor {
        description = "Suggest code refactoring"
        parameters = {
            code: string,
            goal: string
        }
    }

    // Testing
    skill generate_tests {
        description = "Generate test cases"
        parameters = {
            code: string,
            coverage_target: number = 80
        }
    }
}
```

### A: Tool Reference

The MCP server exposes these built-in tools:

| Tool | Description | Parameters |
|------|-------------|------------|
| `neam_run_agent` | Execute a Neam agent | `agent_id`, `input`, `context` |
| `neam_list_agents` | List available agents | none |
| `neam_get_agent_info` | Get agent details | `agent_id` |
| `neam_execute_skill` | Run a specific skill | `agent_id`, `skill_id`, `params` |
| `neam_search_knowledge` | Search knowledge base | `query`, `kb_id`, `top_k` |
| `neam_voice_transcribe` | Transcribe audio | `audio_path` |
| `neam_voice_synthesize` | Generate speech | `text`, `voice` |

---

## Strategy B: Channel Gateway

### B: Architecture

The Channel Gateway enables Neam agents to communicate across multiple messaging platforms with automatic message formatting and session management.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                      Channel Gateway Architecture                            │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐      │
│  │ WhatsApp │  │ Telegram │  │  Slack   │  │ Discord  │  │  Signal  │      │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘      │
│       │             │             │             │             │              │
│       └─────────────┴──────┬──────┴─────────────┴─────────────┘              │
│                            │                                                 │
│                            ▼                                                 │
│                   ┌────────────────┐                                        │
│                   │    Webhook     │                                        │
│                   │   Handlers     │                                        │
│                   └───────┬────────┘                                        │
│                           │                                                 │
│                           ▼                                                 │
│                   ┌────────────────┐                                        │
│                   │    Channel     │                                        │
│                   │   Formatter    │                                        │
│                   └───────┬────────┘                                        │
│                           │                                                 │
│            ┌──────────────┼──────────────┐                                  │
│            │              │              │                                  │
│            ▼              ▼              ▼                                  │
│     ┌───────────┐  ┌───────────┐  ┌───────────┐                            │
│     │  Session  │  │   A2A     │  │   Neam    │                            │
│     │  Manager  │  │  Client   │  │  Runtime  │                            │
│     └───────────┘  └───────────┘  └───────────┘                            │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### B: Installation

#### npm Package

```bash
npm install @neam/openclaw-gateway
# or
yarn add @neam/openclaw-gateway
```

#### From Source

```bash
cd integrations/openclaw-gateway
npm install
npm run build
```

### B: Configuration

#### Basic Configuration

```typescript
import { createGateway } from '@neam/openclaw-gateway';

const gateway = await createGateway({
  // Required
  agentId: 'customer-support',
  neamServerUrl: 'http://localhost:8080',

  // Optional
  sessionTtlMs: 3600000,        // 1 hour session timeout
  maxHistoryLength: 50,         // Messages to retain
  enableTypingIndicator: true,  // Show typing status

  // Channel-specific
  channels: {
    whatsapp: {
      phoneNumberId: process.env.WHATSAPP_PHONE_ID,
      accessToken: process.env.WHATSAPP_TOKEN,
      verifyToken: process.env.WHATSAPP_VERIFY,
    },
    telegram: {
      botToken: process.env.TELEGRAM_BOT_TOKEN,
    },
    slack: {
      botToken: process.env.SLACK_BOT_TOKEN,
      signingSecret: process.env.SLACK_SIGNING_SECRET,
    },
    discord: {
      botToken: process.env.DISCORD_BOT_TOKEN,
      applicationId: process.env.DISCORD_APP_ID,
    },
  },
});
```

#### Environment Variables

```bash
# .env file
NEAM_SERVER_URL=http://localhost:8080
NEAM_AGENT_ID=customer-support

# WhatsApp Business API
WHATSAPP_PHONE_ID=your_phone_number_id
WHATSAPP_TOKEN=your_access_token
WHATSAPP_VERIFY=your_verify_token

# Telegram
TELEGRAM_BOT_TOKEN=your_bot_token

# Slack
SLACK_BOT_TOKEN=xoxb-your-token
SLACK_SIGNING_SECRET=your_signing_secret

# Discord
DISCORD_BOT_TOKEN=your_bot_token
DISCORD_APP_ID=your_application_id
```

### B: Usage Examples

#### Example 1: Express.js Integration

```typescript
import express from 'express';
import { createGateway, WhatsAppWebhook, TelegramWebhook } from '@neam/openclaw-gateway';

const app = express();
app.use(express.json());

// Create gateway
const gateway = await createGateway({
  agentId: 'support-agent',
  neamServerUrl: 'http://localhost:8080',
});

// WhatsApp webhook
const whatsapp = new WhatsAppWebhook({
  phoneNumberId: process.env.WHATSAPP_PHONE_ID!,
  accessToken: process.env.WHATSAPP_TOKEN!,
  verifyToken: process.env.WHATSAPP_VERIFY!,
});

app.get('/webhook/whatsapp', (req, res) => {
  // Verification challenge
  const mode = req.query['hub.mode'];
  const token = req.query['hub.verify_token'];
  const challenge = req.query['hub.challenge'];

  if (mode === 'subscribe' && token === process.env.WHATSAPP_VERIFY) {
    res.status(200).send(challenge);
  } else {
    res.sendStatus(403);
  }
});

app.post('/webhook/whatsapp', async (req, res) => {
  try {
    const messages = whatsapp.parseWebhook(req.body);

    for (const msg of messages) {
      // Convert to channel message
      const channelMsg = {
        messageId: msg.id,
        channelId: msg.from,
        channelType: 'whatsapp' as const,
        userId: msg.from,
        content: msg.text?.body || '',
        timestamp: Date.now(),
      };

      // Process through gateway
      const responses = await gateway.processMessage(channelMsg);

      // Send responses back
      for (const response of responses) {
        await whatsapp.sendMessage(msg.from, response.content);
      }
    }

    res.sendStatus(200);
  } catch (error) {
    console.error('WhatsApp webhook error:', error);
    res.sendStatus(500);
  }
});

// Telegram webhook
const telegram = new TelegramWebhook({
  botToken: process.env.TELEGRAM_BOT_TOKEN!,
});

app.post('/webhook/telegram', async (req, res) => {
  try {
    const update = req.body;

    if (update.message) {
      const msg = update.message;

      const channelMsg = {
        messageId: String(msg.message_id),
        channelId: String(msg.chat.id),
        channelType: 'telegram' as const,
        userId: String(msg.from.id),
        content: msg.text || '',
        timestamp: msg.date * 1000,
      };

      const responses = await gateway.processMessage(channelMsg);

      for (const response of responses) {
        await telegram.sendMessage(msg.chat.id, response.content);
      }
    }

    res.sendStatus(200);
  } catch (error) {
    console.error('Telegram webhook error:', error);
    res.sendStatus(500);
  }
});

app.listen(3000, () => {
  console.log('Gateway running on port 3000');
});
```

#### Example 2: Multi-Agent Routing

```typescript
import { createGateway, SessionStore } from '@neam/openclaw-gateway';

// Create session store
const sessions = new SessionStore({
  ttlMs: 3600000,
  maxSessions: 10000,
});

// Create gateways for different agents
const supportGateway = await createGateway({
  agentId: 'customer-support',
  neamServerUrl: 'http://localhost:8080',
});

const salesGateway = await createGateway({
  agentId: 'sales-assistant',
  neamServerUrl: 'http://localhost:8080',
});

const techGateway = await createGateway({
  agentId: 'tech-support',
  neamServerUrl: 'http://localhost:8080',
});

// Route messages to appropriate agent
async function routeMessage(message: ChannelMessage) {
  // Get or create session
  let session = sessions.get(message.channelId);

  if (!session) {
    session = sessions.create({
      channelId: message.channelId,
      channelType: message.channelType,
      userId: message.userId,
    });
  }

  // Determine agent based on session state or message content
  let gateway = supportGateway;

  if (session.metadata?.department === 'sales') {
    gateway = salesGateway;
  } else if (session.metadata?.department === 'tech') {
    gateway = techGateway;
  } else if (message.content.toLowerCase().includes('buy') ||
             message.content.toLowerCase().includes('price')) {
    gateway = salesGateway;
    session.metadata = { ...session.metadata, department: 'sales' };
  } else if (message.content.toLowerCase().includes('error') ||
             message.content.toLowerCase().includes('bug')) {
    gateway = techGateway;
    session.metadata = { ...session.metadata, department: 'tech' };
  }

  return gateway.processMessage(message);
}
```

#### Example 3: Message Formatting

```typescript
import {
  formatForChannel,
  formatAndSplit,
  MESSAGE_LIMITS
} from '@neam/openclaw-gateway';

// Agent response with rich formatting
const agentResponse = `
# Order Status

Your order **#12345** is currently being processed.

## Items:
- Widget Pro x2
- Gadget Plus x1

## Shipping Details:
| Field | Value |
|-------|-------|
| Method | Express |
| ETA | 2-3 days |

For more info, visit [our website](https://example.com).
`;

// Format for WhatsApp (limited markdown support)
const whatsappMsg = formatForChannel(agentResponse, 'whatsapp');
console.log(whatsappMsg);
// Output:
// *Order Status*
//
// Your order *#12345* is currently being processed.
//
// *Items:*
// • Widget Pro x2
// • Gadget Plus x1
// ...

// Format for Telegram (full markdown)
const telegramMsg = formatForChannel(agentResponse, 'telegram');

// Format for Slack (uses mrkdwn)
const slackMsg = formatForChannel(agentResponse, 'slack');

// Handle long messages (auto-split)
const longResponse = '...very long message...';
const parts = formatAndSplit(longResponse, 'whatsapp');
// Returns array of message parts within WhatsApp's 4096 char limit

// Check limits
console.log(MESSAGE_LIMITS);
// { whatsapp: 4096, telegram: 4096, slack: 40000, discord: 2000, ... }
```

#### Example 4: Session Management

```typescript
import { SessionStore, SessionClient } from '@neam/openclaw-gateway';

// Local session store
const localStore = new SessionStore({
  ttlMs: 3600000,
  maxSessions: 10000,
  cleanupIntervalMs: 60000,
});

// Remote session client (for distributed deployments)
const remoteClient = new SessionClient({
  serverUrl: 'http://session-service:8080',
  timeout: 5000,
});

// Create session with context
const session = localStore.create({
  channelId: '+1234567890',
  channelType: 'whatsapp',
  userId: 'user_123',
  metadata: {
    language: 'en',
    timezone: 'America/New_York',
    customerTier: 'premium',
  },
});

// Add conversation history
localStore.addMessage(session.id, {
  role: 'user',
  content: 'Hi, I need help with my order',
});

localStore.addMessage(session.id, {
  role: 'assistant',
  content: 'Hello! I\'d be happy to help. What\'s your order number?',
});

// Get conversation history for context
const history = localStore.getHistory(session.id);

// Transfer session to another agent
localStore.update(session.id, {
  metadata: {
    ...session.metadata,
    transferredTo: 'specialist-agent',
    transferReason: 'Complex technical issue',
  },
});
```

### B: Platform Integration

#### WhatsApp Business API Setup

1. Create a Meta Business Account at [business.facebook.com](https://business.facebook.com)
2. Set up WhatsApp Business API in Meta Developer Console
3. Get your Phone Number ID and Access Token
4. Configure webhook URL: `https://your-domain.com/webhook/whatsapp`
5. Subscribe to `messages` webhook field

```typescript
const whatsapp = new WhatsAppWebhook({
  phoneNumberId: 'YOUR_PHONE_NUMBER_ID',
  accessToken: 'YOUR_ACCESS_TOKEN',
  verifyToken: 'YOUR_VERIFY_TOKEN',

  // Optional: Media handling
  mediaDownloadPath: './media',

  // Optional: Message templates
  templates: {
    welcome: 'hello_world',
    orderUpdate: 'order_status_update',
  },
});

// Send template message
await whatsapp.sendTemplate(phoneNumber, 'orderUpdate', {
  orderId: '12345',
  status: 'shipped',
});

// Send media
await whatsapp.sendImage(phoneNumber, 'https://example.com/product.jpg', 'Product Image');
```

#### Telegram Bot Setup

1. Create bot via [@BotFather](https://t.me/BotFather)
2. Get bot token
3. Set webhook: `https://api.telegram.org/bot<TOKEN>/setWebhook?url=https://your-domain.com/webhook/telegram`

```typescript
const telegram = new TelegramWebhook({
  botToken: 'YOUR_BOT_TOKEN',

  // Optional: Inline keyboards
  enableInlineKeyboards: true,

  // Optional: Commands
  commands: [
    { command: 'start', description: 'Start conversation' },
    { command: 'help', description: 'Get help' },
    { command: 'status', description: 'Check order status' },
  ],
});

// Send message with inline keyboard
await telegram.sendMessage(chatId, 'How can I help you?', {
  reply_markup: {
    inline_keyboard: [
      [{ text: 'Order Status', callback_data: 'order_status' }],
      [{ text: 'Contact Support', callback_data: 'support' }],
    ],
  },
});
```

#### Slack App Setup

1. Create app at [api.slack.com/apps](https://api.slack.com/apps)
2. Add Bot Token Scopes: `chat:write`, `app_mentions:read`, `im:history`
3. Install to workspace
4. Set up Event Subscriptions URL

```typescript
const slack = new SlackWebhook({
  botToken: 'xoxb-YOUR-TOKEN',
  signingSecret: 'YOUR_SIGNING_SECRET',

  // Optional: App mention handling
  respondToMentions: true,

  // Optional: Slash commands
  slashCommands: {
    '/neam': handleNeamCommand,
  },
});

// Send rich message with blocks
await slack.sendMessage(channelId, {
  text: 'Order Update',
  blocks: [
    {
      type: 'header',
      text: { type: 'plain_text', text: 'Order #12345' },
    },
    {
      type: 'section',
      fields: [
        { type: 'mrkdwn', text: '*Status:*\nShipped' },
        { type: 'mrkdwn', text: '*ETA:*\n2-3 days' },
      ],
    },
    {
      type: 'actions',
      elements: [
        {
          type: 'button',
          text: { type: 'plain_text', text: 'Track Order' },
          url: 'https://example.com/track/12345',
        },
      ],
    },
  ],
});
```

---

## Strategy C: A2A Federation

### C: Architecture

The A2A Federation enables distributed agent networks where agents can discover, communicate with, and hand off tasks to each other across organizational boundaries.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                       A2A Federation Architecture                            │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│   Organization A                    Organization B                          │
│   ┌─────────────────┐              ┌─────────────────┐                      │
│   │   Instance A    │              │   Instance B    │                      │
│   │  ┌───────────┐  │              │  ┌───────────┐  │                      │
│   │  │  Agent 1  │  │              │  │  Agent 3  │  │                      │
│   │  │  Agent 2  │  │              │  │  Agent 4  │  │                      │
│   │  └───────────┘  │              │  └───────────┘  │                      │
│   │        │        │              │        │        │                      │
│   │   ┌────┴────┐   │              │   ┌────┴────┐   │                      │
│   │   │Registry │   │              │   │Registry │   │                      │
│   │   └────┬────┘   │              │   └────┬────┘   │                      │
│   └────────┼────────┘              └────────┼────────┘                      │
│            │                                │                                │
│            └───────────┬────────────────────┘                                │
│                        │                                                     │
│                        ▼                                                     │
│              ┌─────────────────┐                                            │
│              │ Central Registry│  (Optional)                                │
│              │    Server       │                                            │
│              └─────────────────┘                                            │
│                                                                              │
│   ┌─────────────────────────────────────────────────────────────┐          │
│   │                    Trust Layer                               │          │
│   │  • Domain allowlists    • Token authentication              │          │
│   │  • HTTPS verification   • Trust levels                      │          │
│   └─────────────────────────────────────────────────────────────┘          │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### C: Installation

#### npm Package

```bash
npm install @neam/federation
# or
yarn add @neam/federation
```

#### C++ Integration

```cpp
#include <neamc/federation/registry.hpp>
#include <neamc/federation/client.hpp>
#include <neamc/federation/trust.hpp>
#include <neamc/federation/router.hpp>
```

### C: Configuration

#### Basic Setup

```typescript
import { createFederation } from '@neam/federation';

const { registry, client, trust, router } = createFederation({
  // This instance's URL (how others reach us)
  instanceUrl: 'https://agents.company-a.com',

  // Optional: Central registry for discovery
  registryUrl: 'https://registry.neam.ai',

  // Trust policy
  trustPolicy: {
    minimumLevel: 'verified',
    requireHttps: true,
    verifyCertificates: true,
    allowedDomains: ['*.company-a.com', '*.trusted-partner.com'],
    blockedDomains: ['competitor.com'],
    tokenExpiryMs: 3600000,
  },

  // Router configuration
  routerConfig: {
    preferLocal: true,
    enableLoadBalancing: true,
    enableFallback: true,
    routingCacheTtlMs: 60000,
    healthCheckIntervalMs: 30000,
  },
});
```

#### Registry Configuration

```typescript
import { FederationRegistry } from '@neam/federation';

const registry = new FederationRegistry({
  instanceUrl: 'https://agents.mycompany.com',
  registryUrl: 'https://registry.neam.ai',  // Optional

  heartbeatIntervalMs: 30000,  // 30 seconds
  agentTtlMs: 300000,          // 5 minutes
  enableDiscovery: true,
  enableRegistration: true,
  autoSync: true,
});
```

### C: Usage Examples

#### Example 1: Register and Discover Agents

```typescript
import { createFederation } from '@neam/federation';

// Company A sets up federation
const companyA = createFederation({
  instanceUrl: 'https://agents.company-a.com',
  registryUrl: 'https://shared-registry.example.com',
});

// Register local agents
companyA.registry.registerLocal('sales-agent', {
  name: 'Sales Assistant',
  description: 'Handles sales inquiries and quotes',
  url: 'https://agents.company-a.com/agents/sales-agent',
  capabilities: {
    streaming: true,
    pushNotifications: false,
  },
  skills: [
    {
      id: 'generate-quote',
      name: 'Generate Quote',
      description: 'Creates price quotes for products',
      tags: ['sales', 'pricing'],
    },
    {
      id: 'check-inventory',
      name: 'Check Inventory',
      description: 'Checks product availability',
      tags: ['sales', 'inventory'],
    },
  ],
});

companyA.registry.registerLocal('support-agent', {
  name: 'Customer Support',
  description: 'Handles customer support tickets',
  url: 'https://agents.company-a.com/agents/support-agent',
  capabilities: {
    streaming: true,
    pushNotifications: true,
  },
  skills: [
    {
      id: 'create-ticket',
      name: 'Create Ticket',
      description: 'Creates support tickets',
      tags: ['support', 'ticketing'],
    },
    {
      id: 'check-status',
      name: 'Check Status',
      description: 'Checks ticket status',
      tags: ['support', 'ticketing'],
    },
  ],
});

// Start heartbeats
companyA.registry.startHeartbeat('sales-agent');
companyA.registry.startHeartbeat('support-agent');

// Sync with central registry
await companyA.registry.syncWithRegistry();

// Company B discovers agents
const companyB = createFederation({
  instanceUrl: 'https://agents.company-b.com',
  registryUrl: 'https://shared-registry.example.com',
  trustPolicy: {
    allowedDomains: ['*.company-a.com'],
  },
});

// Find agents by capability
const streamingAgents = companyB.registry.findByCapability('streaming');
console.log('Streaming agents:', streamingAgents.map(a => a.card.name));

// Find agents by skill
const salesAgents = companyB.registry.findBySkill('generate-quote');
console.log('Sales agents:', salesAgents.map(a => a.card.name));

// Find agents by tag
const supportAgents = companyB.registry.findByTag('support');
console.log('Support agents:', supportAgents.map(a => a.card.name));
```

#### Example 2: Cross-Instance Task Execution

```typescript
import { createFederation } from '@neam/federation';

const federation = createFederation({
  instanceUrl: 'https://orchestrator.example.com',
  trustPolicy: {
    allowedDomains: ['*.trusted-partners.com'],
  },
});

// Execute task on remote agent
const response = await federation.router.execute({
  agentId: 'specialist-agent',  // Will be routed to correct instance
  message: {
    role: 'user',
    parts: [{ type: 'text', text: 'Analyze this legal contract...' }],
  },
  context: {
    userId: 'user-123',
    requestId: 'req-456',
    priority: 'high',
  },
});

console.log('Task ID:', response.taskId);
console.log('State:', response.state);
console.log('Messages:', response.messages);

// Execute with fallback (tries alternative instances on failure)
const reliableResponse = await federation.router.executeWithFallback({
  agentId: 'critical-agent',
  message: {
    role: 'user',
    parts: [{ type: 'text', text: 'Process this urgent request' }],
  },
});

// Route by capability (finds any agent with the capability)
const capabilityDecision = federation.router.routeByCapability('streaming');
if (capabilityDecision.target !== 'notFound') {
  const streamResponse = await federation.router.execute({
    agentId: capabilityDecision.agentId,
    message: { role: 'user', parts: [{ type: 'text', text: 'Stream this...' }] },
  });
}

// Route by skill (finds agent with specific skill)
const skillDecision = federation.router.routeBySkill('legal-analysis');
console.log('Routed to:', skillDecision.instanceUrl);
```

#### Example 3: Agent Handoffs

```typescript
import { createFederation } from '@neam/federation';

const federation = createFederation({
  instanceUrl: 'https://agents.example.com',
});

// Simple handoff
const result = await federation.router.handoff({
  fromAgent: 'triage-agent',
  toAgent: 'specialist-agent',
  context: {
    customerId: 'cust-123',
    issue: 'billing-dispute',
    severity: 'high',
  },
  conversationHistory: [
    { role: 'user', content: 'I have a billing question' },
    { role: 'assistant', content: 'I\'ll transfer you to our billing specialist' },
  ],
  reason: 'Customer needs billing specialist for dispute resolution',
});

console.log('Handoff result:', result.state);

// Chained handoff (A → B → C)
async function handleComplexRequest(request: any) {
  // Step 1: Triage
  const triageResult = await federation.router.execute({
    agentId: 'triage-agent',
    message: { role: 'user', parts: [{ type: 'text', text: request.message }] },
  });

  // Step 2: Analyze (handoff to analyst)
  const analysisResult = await federation.router.handoff({
    fromAgent: 'triage-agent',
    toAgent: 'analysis-agent',
    context: { triageResult: triageResult.messages },
    reason: 'Requires detailed analysis',
  });

  // Step 3: Resolve (handoff to resolver)
  const resolutionResult = await federation.router.handoff({
    fromAgent: 'analysis-agent',
    toAgent: 'resolution-agent',
    context: {
      triageResult: triageResult.messages,
      analysisResult: analysisResult.messages,
    },
    reason: 'Ready for resolution',
  });

  return resolutionResult;
}
```

#### Example 4: Multi-Organization Collaboration

```typescript
// Organization A: E-commerce Platform
const ecommerce = createFederation({
  instanceUrl: 'https://agents.ecommerce.com',
  registryUrl: 'https://partner-registry.example.com',
  trustPolicy: {
    allowedDomains: ['*.logistics-partner.com', '*.payment-partner.com'],
  },
});

ecommerce.registry.registerLocal('order-agent', {
  name: 'Order Management',
  description: 'Handles order processing',
  url: 'https://agents.ecommerce.com/agents/order-agent',
  skills: [
    { id: 'create-order', name: 'Create Order', description: 'Creates new orders' },
    { id: 'update-order', name: 'Update Order', description: 'Updates existing orders' },
  ],
});

// Organization B: Logistics Partner
const logistics = createFederation({
  instanceUrl: 'https://agents.logistics-partner.com',
  registryUrl: 'https://partner-registry.example.com',
  trustPolicy: {
    allowedDomains: ['*.ecommerce.com'],
  },
});

logistics.registry.registerLocal('shipping-agent', {
  name: 'Shipping Coordinator',
  description: 'Manages shipping and delivery',
  url: 'https://agents.logistics-partner.com/agents/shipping-agent',
  skills: [
    { id: 'calculate-shipping', name: 'Calculate Shipping', description: 'Calculates shipping costs' },
    { id: 'track-package', name: 'Track Package', description: 'Tracks package location' },
    { id: 'schedule-delivery', name: 'Schedule Delivery', description: 'Schedules delivery time' },
  ],
});

// Organization C: Payment Partner
const payments = createFederation({
  instanceUrl: 'https://agents.payment-partner.com',
  registryUrl: 'https://partner-registry.example.com',
  trustPolicy: {
    allowedDomains: ['*.ecommerce.com'],
    minimumLevel: 'trusted',  // Higher security for payments
  },
});

payments.registry.registerLocal('payment-agent', {
  name: 'Payment Processor',
  description: 'Handles payment processing',
  url: 'https://agents.payment-partner.com/agents/payment-agent',
  skills: [
    { id: 'process-payment', name: 'Process Payment', description: 'Processes payments' },
    { id: 'refund', name: 'Issue Refund', description: 'Issues refunds' },
  ],
});

// E-commerce orchestrates across partners
async function processOrder(orderData: any) {
  // Calculate shipping via logistics partner
  const shippingCost = await ecommerce.router.execute({
    agentId: 'shipping-agent',  // Remote agent at logistics partner
    message: {
      role: 'user',
      parts: [{ type: 'text', text: `Calculate shipping for: ${JSON.stringify(orderData)}` }],
    },
  });

  // Process payment via payment partner
  const paymentResult = await ecommerce.router.execute({
    agentId: 'payment-agent',  // Remote agent at payment partner
    message: {
      role: 'user',
      parts: [{ type: 'text', text: `Process payment: ${orderData.total + shippingCost}` }],
    },
  });

  // Schedule delivery via logistics partner
  const deliverySchedule = await ecommerce.router.execute({
    agentId: 'shipping-agent',
    message: {
      role: 'user',
      parts: [{ type: 'text', text: `Schedule delivery for order ${orderData.orderId}` }],
    },
  });

  return {
    orderId: orderData.orderId,
    shippingCost,
    paymentResult,
    deliverySchedule,
  };
}
```

### C: Trust Management

#### Trust Levels

```typescript
type TrustLevel = 'blocked' | 'unknown' | 'verified' | 'trusted';
```

| Level | Description | Typical Use |
|-------|-------------|-------------|
| `blocked` | Explicitly denied | Competitors, known bad actors |
| `unknown` | No trust established | New instances, public internet |
| `verified` | Identity confirmed | Partners with signed certificates |
| `trusted` | Full trust | Internal instances, close partners |

#### Configuring Trust

```typescript
import { TrustManager } from '@neam/federation';

const trust = new TrustManager({
  minimumLevel: 'verified',
  requireHttps: true,
  verifyCertificates: true,
  allowedDomains: ['*.mycompany.com', 'partner.example.com'],
  blockedDomains: ['competitor.com', '*.untrusted.net'],
  tokenExpiryMs: 3600000,
});

// Evaluate trust for an instance
const level = trust.evaluate('https://agents.partner.com');
console.log('Trust level:', level);  // 'verified'

// Check if allowed
if (trust.isAllowed('https://agents.partner.com')) {
  // Proceed with communication
}

// Manage trust manually
trust.trustInstance('https://internal.mycompany.com');     // Set to 'trusted'
trust.verifyInstance('https://partner.example.com');        // Set to 'verified'
trust.blockInstance('https://suspicious.com');              // Set to 'blocked'

// Domain-level management
trust.allowDomain('*.new-partner.com');
trust.blockDomain('spam-domain.com');

// Token management
trust.setAuthToken('https://partner.com', 'secret-token-123');
const token = trust.getAuthToken('https://partner.com');

// Check trust statistics
const stats = trust.getStats();
console.log('Trust stats:', stats);
// {
//   totalInstances: 15,
//   totalTokens: 5,
//   byTrustLevel: { blocked: 2, unknown: 5, verified: 6, trusted: 2 },
//   allowedDomains: ['*.mycompany.com', ...],
//   blockedDomains: ['competitor.com', ...],
// }
```

#### Event Handling

```typescript
// Registry events
registry.on('agent:registered', (event) => {
  console.log('New agent registered:', event.data.agentId);
  console.log('Instance:', event.data.instanceUrl);
});

registry.on('agent:removed', (event) => {
  console.log('Agent removed:', event.data.agentId);
  console.log('Reason:', event.data.reason);
});

registry.on('agent:updated', (event) => {
  console.log('Agent updated:', event.data.agentId);
});

registry.on('sync:started', () => {
  console.log('Starting sync with central registry...');
});

registry.on('sync:completed', () => {
  console.log('Sync complete');
});

registry.on('error', (event) => {
  console.error('Registry error:', event.data.error);
});
```

---

## Combined Deployment

For enterprise deployments, all three strategies can work together:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    Combined Enterprise Architecture                          │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │                         External Interfaces                          │   │
│  ├──────────────────────────────────────────────────────────────────────┤   │
│  │                                                                      │   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐              │   │
│  │  │  Strategy A  │  │  Strategy B  │  │  Strategy C  │              │   │
│  │  │  MCP Server  │  │   Channel    │  │    A2A       │              │   │
│  │  │             │  │   Gateway    │  │  Federation  │              │   │
│  │  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘              │   │
│  │         │                 │                 │                       │   │
│  │         │  IDE/Desktop    │  Messaging      │  Remote Agents       │   │
│  │         │  Integration    │  Platforms      │  Cross-Org           │   │
│  │         │                 │                 │                       │   │
│  └─────────┼─────────────────┼─────────────────┼───────────────────────┘   │
│            │                 │                 │                           │
│            └─────────────────┼─────────────────┘                           │
│                              │                                             │
│                              ▼                                             │
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │                         Neam Runtime Core                            │   │
│  ├──────────────────────────────────────────────────────────────────────┤   │
│  │                                                                      │   │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌────────────┐ │   │
│  │  │   Agents    │  │  Knowledge  │  │    Voice    │  │   State    │ │   │
│  │  │   Engine    │  │    (RAG)    │  │   Pipeline  │  │  Backend   │ │   │
│  │  └─────────────┘  └─────────────┘  └─────────────┘  └────────────┘ │   │
│  │                                                                      │   │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌────────────┐ │   │
│  │  │    LLM      │  │  Telemetry  │  │   Health    │  │    MCP     │ │   │
│  │  │   Gateway   │  │   (OTEL)    │  │   Manager   │  │   Client   │ │   │
│  │  └─────────────┘  └─────────────┘  └─────────────┘  └────────────┘ │   │
│  │                                                                      │   │
│  └──────────────────────────────────────────────────────────────────────┘   │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Example: Full Stack Deployment

```typescript
import { createFederation } from '@neam/federation';
import { createGateway, WhatsAppWebhook } from '@neam/openclaw-gateway';
import express from 'express';

const app = express();
app.use(express.json());

// 1. Set up federation for cross-organization communication
const federation = createFederation({
  instanceUrl: 'https://agents.mycompany.com',
  registryUrl: 'https://registry.neam.ai',
  trustPolicy: {
    minimumLevel: 'verified',
    allowedDomains: ['*.trusted-partners.com'],
  },
});

// Register our agents
federation.registry.registerLocal('support-agent', {
  name: 'Customer Support',
  description: 'Handles customer inquiries',
  url: 'https://agents.mycompany.com/agents/support',
  capabilities: { streaming: true },
  skills: [
    { id: 'answer-question', name: 'Answer Question' },
    { id: 'create-ticket', name: 'Create Ticket' },
  ],
});

federation.registry.startHeartbeat('support-agent');

// 2. Set up channel gateway for messaging platforms
const gateway = await createGateway({
  agentId: 'support-agent',
  neamServerUrl: 'http://localhost:8080',
});

// 3. Set up webhooks for each channel
const whatsapp = new WhatsAppWebhook({
  phoneNumberId: process.env.WHATSAPP_PHONE_ID!,
  accessToken: process.env.WHATSAPP_TOKEN!,
  verifyToken: process.env.WHATSAPP_VERIFY!,
});

// WhatsApp webhook endpoints
app.get('/webhook/whatsapp', (req, res) => {
  const mode = req.query['hub.mode'];
  const token = req.query['hub.verify_token'];
  const challenge = req.query['hub.challenge'];

  if (mode === 'subscribe' && token === process.env.WHATSAPP_VERIFY) {
    res.status(200).send(challenge);
  } else {
    res.sendStatus(403);
  }
});

app.post('/webhook/whatsapp', async (req, res) => {
  const messages = whatsapp.parseWebhook(req.body);

  for (const msg of messages) {
    const responses = await gateway.processMessage({
      messageId: msg.id,
      channelId: msg.from,
      channelType: 'whatsapp',
      userId: msg.from,
      content: msg.text?.body || '',
      timestamp: Date.now(),
    });

    for (const response of responses) {
      await whatsapp.sendMessage(msg.from, response.content);
    }
  }

  res.sendStatus(200);
});

// 4. A2A endpoints for federation
app.get('/.well-known/agent.json', (req, res) => {
  res.json(federation.registry.getInstanceCard());
});

app.get('/agents', (req, res) => {
  res.json(federation.registry.listLocal());
});

app.post('/tasks/send', async (req, res) => {
  const { agentId, message, context } = req.body;

  // Route to local agent or federate to remote
  const response = await federation.router.execute({
    agentId,
    message,
    context,
  });

  res.json(response);
});

// 5. Health endpoint
app.get('/health', (req, res) => {
  res.json({
    status: 'healthy',
    version: '0.6.3',
    federation: {
      localAgents: federation.registry.listLocal().length,
      remoteAgents: federation.registry.listRemote().length,
    },
  });
});

app.listen(3000, () => {
  console.log('Neam Enterprise Gateway running on port 3000');
});
```

---

## API Reference

### MCP Server Tools

| Tool | Parameters | Returns |
|------|------------|---------|
| `neam_run_agent` | `agent_id: string`, `input: string`, `context?: object` | `{ output: string, metadata: object }` |
| `neam_list_agents` | none | `AgentInfo[]` |
| `neam_get_agent_info` | `agent_id: string` | `AgentInfo` |
| `neam_execute_skill` | `agent_id: string`, `skill_id: string`, `params: object` | `SkillResult` |
| `neam_search_knowledge` | `query: string`, `kb_id?: string`, `top_k?: number` | `SearchResult[]` |

### Channel Gateway API

| Method | Description |
|--------|-------------|
| `createGateway(config)` | Create gateway instance |
| `gateway.processMessage(msg)` | Process incoming message |
| `gateway.sendResponse(channelId, response)` | Send response to channel |
| `formatForChannel(text, channel)` | Format message for channel |
| `formatAndSplit(text, channel)` | Format and split long messages |

### Federation API

| Method | Description |
|--------|-------------|
| `createFederation(config)` | Create federation components |
| `registry.registerLocal(id, card)` | Register local agent |
| `registry.findAgent(id)` | Find agent by ID |
| `registry.findByCapability(cap)` | Find by capability |
| `registry.findBySkill(skill)` | Find by skill |
| `router.execute(request)` | Execute task |
| `router.handoff(request)` | Hand off to another agent |
| `trust.evaluate(url)` | Evaluate trust level |

---

## Troubleshooting

### Common Issues

#### MCP Server Not Starting

```bash
# Check if port is in use
lsof -i :8080

# Run with debug logging
neam-mcp-server --log-level debug

# Verify agent files
neam-mcp-server --agent-dir ./agents --dry-run
```

#### Channel Gateway Connection Issues

```typescript
// Enable debug logging
const gateway = await createGateway({
  ...config,
  debug: true,
});

// Test webhook manually
curl -X POST http://localhost:3000/webhook/whatsapp \
  -H "Content-Type: application/json" \
  -d '{"test": true}'
```

#### Federation Discovery Failures

```typescript
// Check trust policy
const level = trust.evaluate('https://remote-instance.com');
console.log('Trust level:', level);

// Verify HTTPS
console.log('HTTPS required:', trust.getPolicy().requireHttps);

// Check allowed domains
console.log('Allowed domains:', trust.getPolicy().allowedDomains);

// Test connectivity
const isHealthy = await client.ping('https://remote-instance.com');
console.log('Instance healthy:', isHealthy);
```

### Debug Checklist

1. **MCP Server**
   - [ ] Agent files exist and are valid .neam syntax
   - [ ] Environment variables set (API keys)
   - [ ] No port conflicts
   - [ ] Client configuration points to correct path

2. **Channel Gateway**
   - [ ] Webhook URLs accessible from internet (use ngrok for local dev)
   - [ ] Platform credentials valid and not expired
   - [ ] Webhook verification tokens match
   - [ ] Message format matches platform requirements

3. **Federation**
   - [ ] Instance URLs use HTTPS (if requireHttps: true)
   - [ ] Domain in allowedDomains list
   - [ ] Not in blockedDomains list
   - [ ] Trust level meets minimum requirement
   - [ ] Auth tokens not expired

---

## License

Apache-2.0

---

## Resources

- [Neam Language Documentation](https://neam-lang.github.io/docs)
- [MCP Protocol Specification](https://spec.modelcontextprotocol.io)
- [A2A Protocol Specification](https://google.github.io/A2A)
- [GitHub Repository](https://github.com/neam-lang/Neam)
- [Discord Community](https://discord.gg/neam)
