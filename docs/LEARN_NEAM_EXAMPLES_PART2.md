# Learn Neam by Example - Part 2

Advanced patterns, RAG strategies, packaging, and API deployment.

---

## Table of Contents

### Special Agent Patterns
1. [Chain-of-Thought Agent](#1-chain-of-thought-agent)
2. [Planning Agent](#2-planning-agent)
3. [Socratic Teaching Agent](#3-socratic-teaching-agent)
4. [Red/Blue Team Security](#4-redblue-team-security)
5. [Tool-Using Agent](#5-tool-using-agent)
6. [Consensus Agent](#6-consensus-agent)

### RAG Retrieval Strategies
7. [Basic Vector Search](#7-basic-vector-search)
8. [MMR - Maximal Marginal Relevance](#8-mmr---maximal-marginal-relevance)
9. [Hybrid Search](#9-hybrid-search)
10. [HyDE - Hypothetical Document Embeddings](#10-hyde---hypothetical-document-embeddings)
11. [Self-RAG with Relevance Check](#11-self-rag-with-relevance-check)
12. [CRAG - Corrective RAG](#12-crag---corrective-rag)
13. [Agentic RAG](#13-agentic-rag)

### Projects & Packaging
14. [Creating a Project](#14-creating-a-project)
15. [Understanding neam.toml](#15-understanding-neamtoml)
16. [Managing Dependencies](#16-managing-dependencies)
17. [Version Constraints](#17-version-constraints)
18. [Publishing a Package](#18-publishing-a-package)

### Web API Deployment
19. [Starting the API Server](#19-starting-the-api-server)
20. [Querying Agents via REST](#20-querying-agents-via-rest)
21. [Custom Agent Endpoints](#21-custom-agent-endpoints)
22. [RAG-Enabled API Agents](#22-rag-enabled-api-agents)
23. [Production Deployment](#23-production-deployment)

---

## Special Agent Patterns

### 1. Chain-of-Thought Agent

**Difficulty:** Intermediate
**Time:** 10 minutes
**What you'll learn:** Explicit step-by-step reasoning, structured thinking

```neam
// chain_of_thought.neam
// Agent that shows its reasoning process explicitly

agent Reasoner {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "You are a logical reasoner. For every problem:
1. State your understanding of the problem
2. Break it into smaller steps
3. Solve each step showing your work
4. Combine steps into final answer
5. Verify your answer makes sense

Always prefix your thinking with 'THINKING:' and your final answer with 'ANSWER:'"
}

{
  emit "=== Chain-of-Thought Reasoning ===\n";

  // Math problem requiring multiple steps
  let math_problem = "A train travels 120 km in 2 hours. It then travels
                      another 90 km in 1.5 hours. What is the average
                      speed for the entire journey?";

  emit "Problem: " + math_problem + "\n";

  let solution = Reasoner.ask(math_problem);
  emit solution;

  emit "\n--- Logic Problem ---\n";

  // Logic puzzle
  let logic_problem = "If all roses are flowers, and some flowers fade
                       quickly, can we conclude that some roses fade quickly?";

  emit "Problem: " + logic_problem + "\n";

  let logic_solution = Reasoner.ask(logic_problem);
  emit logic_solution;
}
```

**Explanation:**

| Line | Code | What it does |
|------|------|--------------|
| 3-13 | `agent Reasoner { ... }` | Defines agent with explicit reasoning instructions |
| 6-11 | `system: "..."` | Multi-line system prompt with numbered steps |
| 18-20 | `let math_problem = ...` | Multi-line string assignment |
| 24 | `Reasoner.ask(math_problem)` | Agent processes with visible reasoning |
| 30-31 | Logic puzzle | Second example showing same pattern |

**Key Concepts:**
- System prompts can enforce thinking structures
- Multi-line strings maintain formatting
- Same agent can handle different problem types
- Prefixes (`THINKING:`, `ANSWER:`) make output parseable

**Expected Output:**
```
=== Chain-of-Thought Reasoning ===

Problem: A train travels 120 km in 2 hours...

THINKING:
1. Understanding: Find average speed for a two-part journey
2. Step 1: Total distance = 120 + 90 = 210 km
3. Step 2: Total time = 2 + 1.5 = 3.5 hours
4. Step 3: Average speed = Total distance / Total time
5. Calculation: 210 / 3.5 = 60 km/h
6. Verification: 60 × 3.5 = 210 ✓

ANSWER: The average speed for the entire journey is 60 km/h.

--- Logic Problem ---

THINKING:
1. Premise 1: All roses are flowers (Roses ⊂ Flowers)
2. Premise 2: Some flowers fade quickly (∃ flowers that fade)
3. Question: Do some roses fade quickly?
4. Analysis: The flowers that fade could be non-rose flowers
5. We cannot determine which flowers fade - could be tulips, not roses

ANSWER: No, we cannot conclude that some roses fade quickly. This is
a logical fallacy (affirming the consequent).
```

---

### 2. Planning Agent

**Difficulty:** Intermediate
**Time:** 12 minutes
**What you'll learn:** Goal decomposition, task planning, progress monitoring

```neam
// planning_agent.neam
// Agent that creates and executes structured plans

agent Planner {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "You are a strategic planner. When given a goal:
1. Analyze the goal and identify key requirements
2. Break it into 3-5 actionable tasks
3. Order tasks by dependency (what must happen first)
4. Estimate complexity: LOW, MEDIUM, HIGH
5. Identify potential blockers

Output format:
GOAL ANALYSIS: [your analysis]
TASKS:
1. [task] - Complexity: [level] - Depends on: [none/task#]
2. [task] - Complexity: [level] - Depends on: [none/task#]
...
BLOCKERS: [potential issues]
FIRST STEP: [immediate action to take]"
}

agent Executor {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "You execute tasks. Given a task, provide:
1. APPROACH: How you would complete it
2. ACTIONS: Specific steps (as if you did them)
3. RESULT: The outcome
4. STATUS: COMPLETE or BLOCKED [reason]"
}

agent Monitor {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "You monitor project progress. Given completed tasks and
remaining tasks, assess:
1. PROGRESS: Percentage complete
2. ON_TRACK: Yes/No
3. RISKS: Any emerging issues
4. RECOMMENDATION: Next priority"
}

{
  emit "=== Planning Agent System ===\n";

  let goal = "Launch a simple personal blog website";
  emit "Goal: " + goal + "\n";

  // Step 1: Create the plan
  emit "--- Phase 1: Planning ---\n";
  let plan = Planner.ask(goal);
  emit plan + "\n";

  // Step 2: Execute first two tasks (simulated)
  emit "--- Phase 2: Execution ---\n";

  let task1 = "Choose a blogging platform (WordPress, Ghost, or static site)";
  emit "Executing: " + task1 + "\n";
  let result1 = Executor.ask("Execute task: " + task1);
  emit result1 + "\n";

  let task2 = "Set up hosting and domain name";
  emit "Executing: " + task2 + "\n";
  let result2 = Executor.ask("Execute task: " + task2 +
                             "\nContext: Previous task chose Ghost platform");
  emit result2 + "\n";

  // Step 3: Monitor progress
  emit "--- Phase 3: Monitoring ---\n";
  let status = Monitor.ask("Completed: Platform selection, Hosting setup
Remaining: Content creation, Design customization, Launch
Original goal: " + goal);
  emit status;
}
```

**Explanation:**

| Line | Code | What it does |
|------|------|--------------|
| 3-18 | `agent Planner` | Creates structured plans with dependencies |
| 20-26 | `agent Executor` | Executes individual tasks with status |
| 28-36 | `agent Monitor` | Tracks progress and identifies risks |
| 43-44 | Goal definition | Clear, specific goal statement |
| 48 | `Planner.ask(goal)` | Generates the full project plan |
| 55-57 | Task execution | Executor handles individual tasks |
| 60-62 | Context passing | Results flow between tasks |
| 67-69 | Progress check | Monitor assesses overall status |

**Key Concepts:**
- Separation of concerns: Planning, Execution, Monitoring
- Context flows between agents via string concatenation
- Structured output formats enable parsing
- Dependency tracking prevents blocked tasks

**Expected Output:**
```
=== Planning Agent System ===

Goal: Launch a simple personal blog website

--- Phase 1: Planning ---

GOAL ANALYSIS: Creating a personal blog requires platform selection,
hosting, content, and design. Focus on simplicity for quick launch.

TASKS:
1. Choose blogging platform - Complexity: LOW - Depends on: none
2. Set up hosting and domain - Complexity: MEDIUM - Depends on: 1
3. Create initial content (3-5 posts) - Complexity: MEDIUM - Depends on: 2
4. Customize design/theme - Complexity: LOW - Depends on: 2
5. Launch and test - Complexity: LOW - Depends on: 3, 4

BLOCKERS: Domain availability, hosting costs, content ideas
FIRST STEP: Research and decide on platform (WordPress vs Ghost vs Hugo)

--- Phase 2: Execution ---

Executing: Choose a blogging platform...

APPROACH: Compare top 3 platforms on ease of use, cost, features
ACTIONS:
- Reviewed WordPress (complex, plugin-heavy)
- Reviewed Ghost (clean, focused on writing)
- Reviewed Hugo (technical, very fast)
RESULT: Selected Ghost - best balance of simplicity and features
STATUS: COMPLETE

--- Phase 3: Monitoring ---

PROGRESS: 40% complete
ON_TRACK: Yes
RISKS: Content creation may take longer than expected
RECOMMENDATION: Start drafting blog posts while design is customized
```

---

### 3. Socratic Teaching Agent

**Difficulty:** Intermediate
**Time:** 10 minutes
**What you'll learn:** Question-driven learning, guided discovery

```neam
// socratic_teacher.neam
// Agent that teaches through questions rather than direct answers

agent SocraticTeacher {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "You are a Socratic teacher. NEVER give direct answers.
Instead:
1. Ask a probing question that leads toward understanding
2. Build on the student's response with follow-up questions
3. Guide them to discover the answer themselves
4. Only confirm when they reach the correct understanding

Start each response with a thought-provoking question.
Use phrases like:
- 'What do you think would happen if...?'
- 'Can you think of an example where...?'
- 'What's the relationship between...?'
- 'Why might that be the case?'"
}

agent Student {
  provider: "openai",
  model: "gpt-4o-mini",
  temperature: 0.8,
  system: "You are a curious student learning about a topic.
Respond naturally to questions. Sometimes you know the answer,
sometimes you're uncertain. Show your thinking process.
Keep responses brief (2-3 sentences)."
}

{
  emit "=== Socratic Teaching Session ===\n";
  emit "Topic: Why is the sky blue?\n\n";

  // Initial question from student
  let student_q = "Teacher, why is the sky blue?";
  emit "Student: " + student_q + "\n";

  // Socratic dialogue loop
  let teacher_r1 = SocraticTeacher.ask(student_q);
  emit "Teacher: " + teacher_r1 + "\n";

  let student_r1 = Student.ask("The teacher asked: " + teacher_r1 +
                               "\nRespond as a student learning about light and sky.");
  emit "Student: " + student_r1 + "\n";

  let teacher_r2 = SocraticTeacher.ask("Student said: " + student_r1 +
                                       "\nContinue guiding them to understand light scattering.");
  emit "Teacher: " + teacher_r2 + "\n";

  let student_r2 = Student.ask("The teacher asked: " + teacher_r2);
  emit "Student: " + student_r2 + "\n";

  let teacher_r3 = SocraticTeacher.ask("Student said: " + student_r2 +
                                       "\nThey're getting close. One more guiding question.");
  emit "Teacher: " + teacher_r3 + "\n";

  let student_final = Student.ask("The teacher asked: " + teacher_r3 +
                                  "\nTry to synthesize what you've learned.");
  emit "Student: " + student_final + "\n";

  emit "\n=== Learning Complete ===";
}
```

**Explanation:**

| Line | Code | What it does |
|------|------|--------------|
| 3-17 | `agent SocraticTeacher` | Never gives answers, only asks questions |
| 19-25 | `agent Student` | Simulates learning process with uncertainty |
| 22 | `temperature: 0.8` | Higher creativity for varied responses |
| 33-34 | Initial question | Student poses the learning topic |
| 37-38 | First exchange | Teacher responds with guiding question |
| 40-42 | Context chaining | Previous response fed into next prompt |
| 50-51 | Synthesis | Student attempts to form understanding |

**Key Concepts:**
- Teaching through questions deepens understanding
- Multi-turn dialogue builds on previous context
- Temperature affects response variety
- Role-specific system prompts drive behavior

**Expected Output:**
```
=== Socratic Teaching Session ===

Topic: Why is the sky blue?

Student: Teacher, why is the sky blue?

Teacher: That's a wonderful question! Before we explore that,
what do you know about what light is made of? Have you ever
seen a rainbow or light through a prism?

Student: Oh yes! A rainbow has many colors - red, orange,
yellow, green, blue, violet. I think white light is actually
made of all these colors combined?

Teacher: Excellent observation! Now, if white sunlight contains
all these colors, what might be happening to separate them in
our atmosphere? What do you know about how light interacts
with tiny particles?

Student: Hmm, maybe the particles in the air are doing
something to the light? Like splitting it up somehow? But
why would only blue get through to our eyes?

Teacher: You're on the right track! Here's a hint: not all
colors scatter equally. If smaller wavelengths scatter more
easily, and blue light has a shorter wavelength than red,
what conclusion can you draw?

Student: Oh! So blue light scatters more because it has
shorter wavelengths! The atmosphere scatters blue light in
all directions, so we see blue no matter where we look in
the sky. The other colors pass through more directly!

=== Learning Complete ===
```

---

### 4. Red/Blue Team Security

**Difficulty:** Advanced
**Time:** 15 minutes
**What you'll learn:** Adversarial analysis, security assessment, defense strategies

```neam
// red_blue_team.neam
// Security analysis with attack and defense perspectives

agent RedTeam {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "You are a Red Team security analyst (ethical attacker).
Your role is to identify vulnerabilities and potential attack vectors.
For any system or code:
1. Identify the attack surface
2. List potential vulnerabilities (OWASP Top 10 focus)
3. Describe exploitation scenarios
4. Rate severity: CRITICAL, HIGH, MEDIUM, LOW
5. Be specific and technical

Format:
ATTACK SURFACE: [entry points]
VULNERABILITIES:
- [V1]: [description] - Severity: [level]
- [V2]: [description] - Severity: [level]
EXPLOITATION: [how an attacker would exploit]"
}

agent BlueTeam {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "You are a Blue Team security analyst (defender).
Your role is to analyze Red Team findings and propose defenses.
For each vulnerability:
1. Validate if it's a real threat
2. Propose specific mitigations
3. Recommend security controls
4. Prioritize by impact and effort

Format:
THREAT VALIDATION: [confirmed/partial/false positive]
MITIGATIONS:
- [V1]: [specific fix] - Effort: [level]
SECURITY CONTROLS: [additional measures]
PRIORITY ORDER: [which to fix first]"
}

agent SecurityLead {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "You are a Security Lead who synthesizes Red and Blue team
findings into executive recommendations.
Provide:
1. RISK SUMMARY: Overall risk level
2. TOP 3 ACTIONS: Immediate priorities
3. TIMELINE: Suggested remediation schedule
4. RESIDUAL RISK: What remains after fixes"
}

{
  emit "=== Security Assessment: Login System ===\n";

  // Target system description
  let system_desc = "Web login form that:
- Accepts username/password via POST
- Queries MySQL: SELECT * FROM users WHERE username='$user' AND password='$pass'
- Sets session cookie on success
- Shows 'Invalid credentials' on failure
- No rate limiting implemented
- Password stored as MD5 hash";

  emit "Target System:\n" + system_desc + "\n";

  // Red Team Analysis
  emit "\n--- RED TEAM ANALYSIS ---\n";
  let red_findings = RedTeam.ask("Analyze this system for vulnerabilities:\n" + system_desc);
  emit red_findings + "\n";

  // Blue Team Response
  emit "\n--- BLUE TEAM RESPONSE ---\n";
  let blue_response = BlueTeam.ask("Red Team found these issues:\n" + red_findings +
                                   "\n\nOriginal system:\n" + system_desc);
  emit blue_response + "\n";

  // Executive Summary
  emit "\n--- EXECUTIVE SUMMARY ---\n";
  let summary = SecurityLead.ask("Red Team:\n" + red_findings +
                                 "\n\nBlue Team:\n" + blue_response);
  emit summary;
}
```

**Explanation:**

| Line | Code | What it does |
|------|------|--------------|
| 3-18 | `agent RedTeam` | Identifies vulnerabilities like an attacker |
| 20-35 | `agent BlueTeam` | Proposes defenses for each vulnerability |
| 37-46 | `agent SecurityLead` | Synthesizes into executive actions |
| 50-56 | `system_desc` | Intentionally vulnerable system for analysis |
| 61-62 | Red Team analysis | Finds SQL injection, weak hashing, etc. |
| 66-68 | Blue Team response | Proposes parameterized queries, bcrypt, etc. |
| 72-74 | Executive summary | Prioritized action plan |

**Key Concepts:**
- Adversarial thinking improves security
- Multiple perspectives catch more issues
- Structured output aids decision-making
- Context aggregation builds complete picture

**Expected Output:**
```
=== Security Assessment: Login System ===

Target System:
Web login form that:
- Accepts username/password via POST
- Queries MySQL: SELECT * FROM users WHERE username='$user'...

--- RED TEAM ANALYSIS ---

ATTACK SURFACE: Login form, database connection, session management

VULNERABILITIES:
- SQL Injection: Direct string interpolation in query - Severity: CRITICAL
- Weak Hashing: MD5 is cryptographically broken - Severity: HIGH
- Brute Force: No rate limiting enables password spraying - Severity: HIGH
- User Enumeration: Same error message helps but timing attacks possible - Severity: MEDIUM

EXPLOITATION:
1. SQL Injection: Input `admin'--` as username bypasses password check
2. If hashes leaked, MD5 rainbow tables crack passwords in seconds
3. Automated tools can try 1000s of passwords per minute

--- BLUE TEAM RESPONSE ---

THREAT VALIDATION: All confirmed as real threats

MITIGATIONS:
- SQL Injection: Use parameterized queries/prepared statements - Effort: LOW
- Weak Hashing: Migrate to bcrypt with cost factor 12 - Effort: MEDIUM
- Brute Force: Implement rate limiting (5 attempts/15 min) - Effort: LOW
- Add account lockout after 10 failed attempts - Effort: LOW

SECURITY CONTROLS: WAF, monitoring failed logins, MFA option
PRIORITY ORDER: SQL Injection (immediate), Rate Limiting, Password Rehashing

--- EXECUTIVE SUMMARY ---

RISK SUMMARY: CRITICAL - System is vulnerable to trivial attacks

TOP 3 ACTIONS:
1. Deploy parameterized queries TODAY (prevents SQL injection)
2. Add rate limiting THIS WEEK (prevents brute force)
3. Plan password hash migration (2-week project)

TIMELINE:
- Day 1: SQL injection fix
- Week 1: Rate limiting
- Week 2-3: Hash migration with user password reset

RESIDUAL RISK: LOW after mitigations; recommend adding MFA
```

---

### 5. Tool-Using Agent

**Difficulty:** Advanced
**Time:** 12 minutes
**What you'll learn:** Agent tool definitions, structured tool calls, result handling

```neam
// tool_agent.neam
// Agent with defined tools it can invoke

agent ToolAgent {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "You are an assistant with access to these tools:

AVAILABLE TOOLS:
1. calculator(expression) - Evaluate math expressions
2. weather(city) - Get current weather for a city
3. search(query) - Search for information
4. reminder(time, message) - Set a reminder

When you need to use a tool, output EXACTLY:
TOOL_CALL: tool_name(arguments)

Then wait for the result before continuing.
After receiving TOOL_RESULT, incorporate it into your response.
You can chain multiple tool calls if needed."
}

agent ToolExecutor {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "You simulate tool execution. Given a tool call, return a
realistic result. Format: TOOL_RESULT: [result]

For calculator: return the computed value
For weather: return temp, conditions, humidity
For search: return a brief summary of findings
For reminder: confirm it's set"
}

{
  emit "=== Tool-Using Agent Demo ===\n";

  // Query requiring tool use
  let query = "What's 15% tip on a $47.80 dinner bill, and what's
               the weather like in Seattle right now?";

  emit "User: " + query + "\n";

  // Agent decides what tools to use
  let agent_response = ToolAgent.ask(query);
  emit "Agent: " + agent_response + "\n";

  // Simulate tool execution
  let tool_result1 = ToolExecutor.ask("Execute: calculator(47.80 * 0.15)");
  emit tool_result1 + "\n";

  let tool_result2 = ToolExecutor.ask("Execute: weather(Seattle)");
  emit tool_result2 + "\n";

  // Agent incorporates results
  let final_response = ToolAgent.ask("Previous query: " + query +
                                      "\nTool results:\n" + tool_result1 +
                                      "\n" + tool_result2 +
                                      "\n\nProvide final answer to user.");
  emit "Agent (final): " + final_response;
}
```

**Explanation:**

| Line | Code | What it does |
|------|------|--------------|
| 3-18 | `agent ToolAgent` | Agent aware of available tools |
| 7-11 | Tool definitions | List of tools with signatures |
| 13-16 | Tool call format | Structured output for parsing |
| 20-30 | `agent ToolExecutor` | Simulates real tool execution |
| 38-39 | Multi-tool query | Requires calculator AND weather |
| 45-48 | Tool execution | Each tool called separately |
| 51-55 | Result integration | Agent synthesizes tool outputs |

**Key Concepts:**
- Tools extend agent capabilities
- Structured tool call format enables parsing
- Tool executor can be real APIs or simulations
- Results flow back into agent context

---

### 6. Consensus Agent

**Difficulty:** Advanced
**Time:** 12 minutes
**What you'll learn:** Multiple perspectives, voting mechanisms, conflict resolution

```neam
// consensus_agent.neam
// Multiple agents reach consensus through structured voting

agent Expert1 {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "You are a software architect who values simplicity and
maintainability. Evaluate proposals focusing on long-term maintenance."
}

agent Expert2 {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "You are a performance engineer who values speed and
efficiency. Evaluate proposals focusing on runtime performance."
}

agent Expert3 {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "You are a security specialist who values safety and
reliability. Evaluate proposals focusing on security implications."
}

agent Moderator {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "You are a neutral moderator. Given multiple expert opinions:
1. Identify areas of agreement
2. Highlight key disagreements
3. Synthesize a consensus recommendation
4. Note any unresolved concerns

Format:
AGREEMENT: [shared views]
DISAGREEMENT: [conflicting views]
CONSENSUS: [recommended decision]
CONCERNS: [remaining issues]"
}

{
  emit "=== Consensus Decision Making ===\n";

  let decision = "Should we use microservices or a monolith for our
                  new e-commerce platform? Expected traffic: 10K users/day
                  initially, team size: 5 developers.";

  emit "Decision: " + decision + "\n";

  // Gather expert opinions
  emit "\n--- Expert Opinions ---\n";

  let opinion1 = Expert1.ask("Evaluate: " + decision +
                             "\nProvide: RECOMMENDATION, REASONING, VOTE (microservices/monolith)");
  emit "Architect: " + opinion1 + "\n";

  let opinion2 = Expert2.ask("Evaluate: " + decision +
                             "\nProvide: RECOMMENDATION, REASONING, VOTE (microservices/monolith)");
  emit "Performance Engineer: " + opinion2 + "\n";

  let opinion3 = Expert3.ask("Evaluate: " + decision +
                             "\nProvide: RECOMMENDATION, REASONING, VOTE (microservices/monolith)");
  emit "Security Specialist: " + opinion3 + "\n";

  // Moderator synthesizes
  emit "\n--- Consensus ---\n";
  let consensus = Moderator.ask("Expert opinions:\n\n1. Architect: " + opinion1 +
                                "\n\n2. Performance: " + opinion2 +
                                "\n\n3. Security: " + opinion3);
  emit consensus;
}
```

**Explanation:**

| Line | Code | What it does |
|------|------|--------------|
| 3-21 | Three experts | Different specialized perspectives |
| 23-36 | `agent Moderator` | Synthesizes into consensus |
| 41-43 | Decision context | Real-world constraints included |
| 48-58 | Opinion gathering | Each expert votes with reasoning |
| 62-65 | Consensus building | Moderator finds common ground |

**Key Concepts:**
- Multiple perspectives reduce bias
- Structured voting enables comparison
- Moderator resolves conflicts
- Concerns are captured even in consensus

---

## RAG Retrieval Strategies

### 7. Basic Vector Search

**Difficulty:** Beginner
**Time:** 5 minutes
**What you'll learn:** Standard semantic similarity search

```neam
// rag_basic.neam
// Simple vector similarity search - the foundation of RAG

knowledge ProductDocs {
  vector_store: "usearch",
  embedding_model: "nomic-embed-text",
  chunk_size: 200,
  chunk_overlap: 50,
  sources: [
    { type: "file", path: "./products.md" }
  ],
  retrieval_strategy: "basic",
  top_k: 3
}

agent ProductHelper {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "You help customers find products. Use ONLY the provided
           context to answer. If the answer isn't in the context, say so.",
  connected_knowledge: [ProductDocs]
}

{
  emit "=== Basic Vector Search RAG ===\n";

  let query = "Which laptop is best for video editing?";
  emit "Query: " + query + "\n";

  let answer = ProductHelper.ask(query);
  emit "Answer: " + answer;
}
```

**Explanation:**

| Line | Code | What it does |
|------|------|--------------|
| 3-14 | `knowledge ProductDocs` | Defines the knowledge base |
| 4 | `vector_store: "usearch"` | Uses USearch for vector storage |
| 5 | `embedding_model` | Model for creating embeddings |
| 6-7 | `chunk_size/overlap` | How documents are split |
| 11 | `retrieval_strategy: "basic"` | Standard cosine similarity |
| 12 | `top_k: 3` | Return 3 most similar chunks |
| 20 | `connected_knowledge` | Links agent to knowledge base |

**Key Concepts:**
- `basic` finds chunks most similar to the query
- Good for straightforward factual questions
- Fast and simple - start here, upgrade if needed

---

### 8. MMR - Maximal Marginal Relevance

**Difficulty:** Intermediate
**Time:** 8 minutes
**What you'll learn:** Balancing relevance with diversity

```neam
// rag_mmr.neam
// MMR retrieval for diverse, non-redundant results

knowledge ResearchPapers {
  vector_store: "usearch",
  embedding_model: "nomic-embed-text",
  chunk_size: 300,
  chunk_overlap: 75,
  sources: [
    { type: "file", path: "./papers/*.md" }
  ],
  retrieval_strategy: "mmr",
  top_k: 5,
  mmr_lambda: 0.7  // 1.0 = pure relevance, 0.0 = pure diversity
}

agent ResearchAssistant {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "You synthesize research findings. Present multiple
           perspectives found in the sources.",
  connected_knowledge: [ResearchPapers]
}

{
  emit "=== MMR Retrieval (Diverse Results) ===\n";

  let query = "What are the effects of social media on mental health?";
  emit "Query: " + query + "\n";
  emit "Using MMR with lambda=0.7 (favoring relevance with some diversity)\n\n";

  let answer = ResearchAssistant.ask(query);
  emit "Answer: " + answer;
}
```

**Explanation:**

| Line | Code | What it does |
|------|------|--------------|
| 11 | `retrieval_strategy: "mmr"` | Maximal Marginal Relevance |
| 13 | `mmr_lambda: 0.7` | Balance between relevance and diversity |
| 9 | `"./papers/*.md"` | Wildcard matches multiple files |

**Key Concepts:**
- MMR reduces redundancy in retrieved chunks
- `mmr_lambda: 1.0` = pure relevance (same as basic)
- `mmr_lambda: 0.0` = maximum diversity
- Use for broad topics where you want varied perspectives

**When to use MMR:**
- Research questions with multiple aspects
- When basic retrieval returns repetitive content
- Synthesizing diverse viewpoints

---

### 9. Hybrid Search

**Difficulty:** Intermediate
**Time:** 8 minutes
**What you'll learn:** Combining keyword and semantic search

```neam
// rag_hybrid.neam
// Combines BM25 keyword search with vector similarity

knowledge TechDocs {
  vector_store: "usearch",
  embedding_model: "nomic-embed-text",
  chunk_size: 256,
  chunk_overlap: 64,
  sources: [
    { type: "file", path: "./technical_docs.md" }
  ],
  retrieval_strategy: "hybrid",
  top_k: 4,
  keyword_weight: 0.3,  // 30% keyword, 70% semantic
  semantic_weight: 0.7
}

agent TechSupport {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "You provide technical support. Be precise with error codes
           and specific terminology.",
  connected_knowledge: [TechDocs]
}

{
  emit "=== Hybrid Search (Keyword + Semantic) ===\n";

  // Query with specific error code
  let query = "How do I fix error code E-4012 in the sync module?";
  emit "Query: " + query + "\n";
  emit "Hybrid search catches 'E-4012' (keyword) AND related concepts (semantic)\n\n";

  let answer = TechSupport.ask(query);
  emit "Answer: " + answer;
}
```

**Explanation:**

| Line | Code | What it does |
|------|------|--------------|
| 11 | `retrieval_strategy: "hybrid"` | Combined search mode |
| 13-14 | `keyword_weight/semantic_weight` | Balance between methods |

**Key Concepts:**
- Keyword search finds exact matches (error codes, IDs, names)
- Semantic search finds conceptually related content
- Hybrid combines both for best of both worlds
- Weights should sum to 1.0

**When to use Hybrid:**
- Technical documentation with specific codes/identifiers
- Queries mixing specific terms with general concepts
- When pure semantic search misses exact matches

---

### 10. HyDE - Hypothetical Document Embeddings

**Difficulty:** Advanced
**Time:** 10 minutes
**What you'll learn:** Query expansion through hypothetical answers

```neam
// rag_hyde.neam
// Generates hypothetical answer, uses it to find real documents

knowledge CodeDocs {
  vector_store: "usearch",
  embedding_model: "nomic-embed-text",
  chunk_size: 300,
  chunk_overlap: 75,
  sources: [
    { type: "file", path: "./api_documentation.md" }
  ],
  retrieval_strategy: "hyde",
  top_k: 3,
  num_hypothetical: 2  // Generate 2 hypothetical docs
}

agent APIHelper {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "You help developers use our API. Provide code examples
           when relevant.",
  connected_knowledge: [CodeDocs]
}

{
  emit "=== HyDE Retrieval (Hypothetical Document Embeddings) ===\n";

  // Abstract question - HyDE excels here
  let query = "How do I handle authentication?";
  emit "Query: " + query + "\n";
  emit "HyDE generates a hypothetical answer, then searches with that.\n";
  emit "This bridges the gap between question and answer formats.\n\n";

  let answer = APIHelper.ask(query);
  emit "Answer: " + answer;
}
```

**Explanation:**

| Line | Code | What it does |
|------|------|--------------|
| 11 | `retrieval_strategy: "hyde"` | Hypothetical Document Embeddings |
| 13 | `num_hypothetical: 2` | Generate 2 hypothetical answers |

**How HyDE Works:**
1. User asks: "How do I handle authentication?"
2. LLM generates hypothetical answer: "To authenticate, use the /auth endpoint with your API key..."
3. System embeds the hypothetical answer (not the question)
4. Search finds real docs similar to that hypothetical answer
5. Real docs are provided to the agent

**Key Concepts:**
- Questions and answers have different embeddings
- Generating a hypothetical answer bridges this gap
- Especially good for abstract or conceptual queries
- `num_hypothetical: 2+` can improve coverage

**When to use HyDE:**
- Abstract questions ("How do I...", "What's the best way...")
- Questions that don't contain domain terminology
- When basic search returns irrelevant results

---

### 11. Self-RAG with Relevance Check

**Difficulty:** Advanced
**Time:** 10 minutes
**What you'll learn:** Self-assessment of retrieved document relevance

```neam
// rag_self.neam
// Checks if retrieved documents are actually relevant

knowledge LegalDocs {
  vector_store: "usearch",
  embedding_model: "nomic-embed-text",
  chunk_size: 400,
  chunk_overlap: 100,
  sources: [
    { type: "file", path: "./legal_policies.md" }
  ],
  retrieval_strategy: "self_rag",
  top_k: 5,
  enable_relevance_check: true,
  relevance_threshold: 0.6  // 60% confidence required
}

agent LegalAssistant {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "You provide legal information (not legal advice).
           Only answer based on verified relevant documents.
           State when information is insufficient.",
  connected_knowledge: [LegalDocs]
}

{
  emit "=== Self-RAG with Relevance Checking ===\n";

  let query = "What is our policy on remote work reimbursement?";
  emit "Query: " + query + "\n";
  emit "Self-RAG retrieves docs, then verifies each is actually relevant.\n";
  emit "Irrelevant docs are filtered out before answering.\n\n";

  let answer = LegalAssistant.ask(query);
  emit "Answer: " + answer;
}
```

**Explanation:**

| Line | Code | What it does |
|------|------|--------------|
| 11 | `retrieval_strategy: "self_rag"` | Self-reflective RAG |
| 13 | `enable_relevance_check: true` | Verify document relevance |
| 14 | `relevance_threshold: 0.6` | Minimum relevance score |

**How Self-RAG Works:**
1. Retrieve top_k documents (5 in this case)
2. For each document, ask: "Is this relevant to the query?"
3. Filter out documents below relevance_threshold
4. Only use verified relevant documents for the answer
5. If no documents pass, indicate insufficient information

**Key Concepts:**
- Not all retrieved documents are useful
- Self-assessment filters false positives
- Improves answer accuracy
- May indicate when knowledge is insufficient

**When to use Self-RAG:**
- High-stakes domains (legal, medical, financial)
- When false information is worse than no information
- When your knowledge base has diverse content

---

### 12. CRAG - Corrective RAG

**Difficulty:** Advanced
**Time:** 12 minutes
**What you'll learn:** Query decomposition, iterative correction

```neam
// rag_crag.neam
// Corrective RAG with query decomposition for complex questions

knowledge CompanyWiki {
  vector_store: "usearch",
  embedding_model: "nomic-embed-text",
  chunk_size: 250,
  chunk_overlap: 50,
  sources: [
    { type: "file", path: "./wiki/**/*.md" }
  ],
  retrieval_strategy: "crag",
  top_k: 4,
  enable_query_decomposition: true,
  max_sub_queries: 3
}

agent WikiAssistant {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "You answer questions about our company using the wiki.
           For complex questions, break them down and address each part.",
  connected_knowledge: [CompanyWiki]
}

{
  emit "=== CRAG (Corrective RAG) ===\n";

  // Complex multi-part question
  let query = "What are the Q3 sales figures for the EMEA region,
               and how do they compare to Q2?";

  emit "Query: " + query + "\n";
  emit "CRAG decomposes this into:\n";
  emit "  1. Q3 sales figures for EMEA\n";
  emit "  2. Q2 sales figures for EMEA\n";
  emit "  3. Comparison between the two\n\n";

  let answer = WikiAssistant.ask(query);
  emit "Answer: " + answer;
}
```

**Explanation:**

| Line | Code | What it does |
|------|------|--------------|
| 11 | `retrieval_strategy: "crag"` | Corrective RAG |
| 13 | `enable_query_decomposition: true` | Break complex queries |
| 14 | `max_sub_queries: 3` | Limit decomposition depth |

**How CRAG Works:**
1. Analyze query complexity
2. Decompose into sub-queries if needed
3. Retrieve documents for each sub-query
4. Assess if retrieval was successful
5. If not, try alternative queries (correction)
6. Combine results into comprehensive answer

**Key Concepts:**
- Complex questions need multiple retrievals
- Query decomposition targets each aspect
- Correction mechanism handles retrieval failures
- More thorough but uses more LLM calls

**When to use CRAG:**
- Multi-part questions
- Comparative queries (A vs B)
- When simple queries might miss context

---

### 13. Agentic RAG

**Difficulty:** Expert
**Time:** 15 minutes
**What you'll learn:** Iterative refinement with reflection

```neam
// rag_agentic.neam
// The most sophisticated RAG - iterative with self-reflection

knowledge ResearchCorpus {
  vector_store: "usearch",
  embedding_model: "nomic-embed-text",
  chunk_size: 350,
  chunk_overlap: 100,
  sources: [
    { type: "file", path: "./research/**/*.md" },
    { type: "web", url: "https://docs.example.com/api" }
  ],
  retrieval_strategy: "agentic",
  top_k: 5,
  max_iterations: 3,
  enable_reflection: true,
  confidence_threshold: 0.8
}

agent ResearchAgent {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "You are a thorough research assistant.
           Pursue complete answers through multiple retrieval rounds.
           Reflect on gaps and seek additional information.",
  connected_knowledge: [ResearchCorpus]
}

{
  emit "=== Agentic RAG (Iterative with Reflection) ===\n";

  let query = "What are the long-term implications of transformer
               architecture on natural language processing, and what
               alternatives are being researched?";

  emit "Query: " + query + "\n";
  emit "Agentic RAG will:\n";
  emit "  1. Initial retrieval\n";
  emit "  2. Reflect: Is this answer complete?\n";
  emit "  3. If not, refine query and retrieve again\n";
  emit "  4. Repeat until confident or max_iterations reached\n\n";

  let answer = ResearchAgent.ask(query);
  emit "Answer: " + answer;
}
```

**Explanation:**

| Line | Code | What it does |
|------|------|--------------|
| 11 | `retrieval_strategy: "agentic"` | Full agentic loop |
| 14 | `max_iterations: 3` | Maximum retrieval rounds |
| 15 | `enable_reflection: true` | Self-assess completeness |
| 16 | `confidence_threshold: 0.8` | When to stop iterating |

**How Agentic RAG Works:**
1. **Retrieve**: Get initial documents
2. **Generate**: Draft an answer
3. **Reflect**: "Is this answer complete and accurate?"
4. **If gaps exist**: Formulate follow-up queries
5. **Retrieve again**: Get additional documents
6. **Iterate**: Until confident or max_iterations
7. **Final answer**: Synthesize all gathered information

**Key Concepts:**
- Most powerful but most expensive strategy
- Emulates human research behavior
- Reflection catches gaps and errors
- Best for complex research questions

**When to use Agentic RAG:**
- Deep research questions
- When completeness matters more than speed
- Open-ended exploratory queries
- When you need comprehensive coverage

---

## RAG Strategy Comparison

| Strategy | Complexity | LLM Calls | Best For |
|----------|------------|-----------|----------|
| `basic` | Low | 1 | Simple factual Q&A |
| `mmr` | Low | 1 | Diverse perspectives |
| `hybrid` | Medium | 1 | Mixed keyword/semantic |
| `hyde` | Medium | 2+ | Abstract questions |
| `self_rag` | Medium | 2+ | High-stakes accuracy |
| `crag` | High | 3+ | Complex multi-part |
| `agentic` | High | 3-10+ | Deep research |

---

## Projects & Packaging

### 14. Creating a Project

**Difficulty:** Beginner
**Time:** 5 minutes
**What you'll learn:** Project initialization, directory structure

```bash
# Create a new Neam project
neam-pkg init my-agent-project

# What gets created:
# my-agent-project/
# ├── neam.toml           # Project manifest
# ├── src/
# │   └── main.neam       # Entry point
# ├── tests/
# │   └── test_main.neam  # Test file
# └── .neam/
#     └── packages/       # Dependencies (empty initially)
```

**Generated neam.toml:**

```toml
neam_version = "1.0"

[project]
name = "my-agent-project"
version = "0.1.0"
description = "A Neam agent project"
type = "binary"
authors = []
license = "MIT"

[project.entry_points]
main = "src/main.neam"

[dependencies]
# Add dependencies here

[dev-dependencies]
# Add dev dependencies here
```

**Generated src/main.neam:**

```neam
// main.neam
// Entry point for my-agent-project

{
  emit "Hello from my-agent-project!";
}
```

**Key Concepts:**
- `neam.toml` is the project manifest (like package.json or Cargo.toml)
- `src/main.neam` is the default entry point
- `.neam/packages/` stores installed dependencies
- `tests/` for test files

---

### 15. Understanding neam.toml

**Difficulty:** Beginner
**Time:** 8 minutes
**What you'll learn:** Manifest structure, configuration options

```toml
# neam.toml - Complete example with all fields

neam_version = "1.0"

[project]
name = "smart-support-bot"
version = "1.2.0"
description = "AI-powered customer support agent with RAG"
type = "binary"                    # "binary" or "library"
authors = ["Dev Team <dev@example.com>"]
license = "Apache-2.0"
repository = "https://github.com/org/smart-support-bot"
homepage = "https://support-bot.example.com"
keywords = ["ai", "support", "rag", "customer-service"]

[project.entry_points]
main = "src/main.neam"            # Main entry for binary
api = "src/api.neam"              # Optional: API entry point

[dependencies]
# Core dependencies
agent-utils = "^1.0.0"            # Caret: 1.0.0 to <2.0.0
rag-tools = "~0.5.0"              # Tilde: 0.5.0 to <0.6.0
http-client = ">=1.2.0"           # Greater: 1.2.0 and above

# Git dependencies
custom-agents = { git = "https://github.com/org/custom-agents" }
private-tools = { git = "https://github.com/org/private", branch = "main" }

# Local development dependency
local-utils = { path = "../local-utils" }

[dev-dependencies]
test-framework = "0.1.0"
mock-llm = "^1.0.0"

[agent]
# Default agent configuration
provider = "openai"
model = "gpt-4o-mini"
temperature = 0.7

[knowledge]
# Default knowledge base settings
vector_store = "usearch"
embedding_model = "nomic-embed-text"
chunk_size = 256
chunk_overlap = 64
```

**Explanation:**

| Section | Purpose |
|---------|---------|
| `[project]` | Package identity and metadata |
| `[project.entry_points]` | Executable entry points |
| `[dependencies]` | Runtime dependencies |
| `[dev-dependencies]` | Development-only dependencies |
| `[agent]` | Default agent configuration |
| `[knowledge]` | Default RAG settings |

**Key Concepts:**
- `type: "binary"` for executables, `"library"` for packages
- Multiple entry points allow different execution modes
- Git dependencies for unpublished packages
- Agent/knowledge sections provide project-wide defaults

---

### 16. Managing Dependencies

**Difficulty:** Intermediate
**Time:** 10 minutes
**What you'll learn:** Installing, updating, removing packages

```bash
# Install all dependencies from neam.toml
neam-pkg install

# Install a specific package (adds to neam.toml)
neam-pkg install agent-utils

# Install specific version
neam-pkg install rag-tools@0.5.2

# Install as dev dependency
neam-pkg install --dev test-framework

# Update all packages to latest compatible
neam-pkg update

# Update specific package
neam-pkg update agent-utils

# Remove a package
neam-pkg remove rag-tools

# List installed packages
neam-pkg list

# Check for outdated packages
neam-pkg outdated

# Search for packages
neam-pkg search "rag"

# Get package info
neam-pkg info agent-utils
```

**Using installed packages:**

```neam
// Using a dependency in your code
import { PromptBuilder } from "agent-utils";
import { VectorSearch } from "rag-tools";

agent Helper {
  provider: "openai",
  model: "gpt-4o-mini",
  system: PromptBuilder.create("helpful assistant")
}

{
  let searcher = VectorSearch.new("./docs");
  let context = searcher.find("user query");
  let answer = Helper.ask(context);
  emit answer;
}
```

**Key Concepts:**
- `install` without arguments reads neam.toml
- Packages are stored in `.neam/packages/`
- `neam.lock` locks exact versions for reproducibility
- Use `--dev` for test/development tools

---

### 17. Version Constraints

**Difficulty:** Intermediate
**Time:** 8 minutes
**What you'll learn:** Semantic versioning, constraint syntax

```toml
[dependencies]
# Exact version - only this specific version
exact-pkg = "1.2.3"

# Caret (^) - Compatible updates (most common)
# ^1.2.3 allows 1.2.3 to <2.0.0
# ^0.2.3 allows 0.2.3 to <0.3.0 (0.x is special)
caret-pkg = "^1.2.3"

# Tilde (~) - Patch updates only
# ~1.2.3 allows 1.2.3 to <1.3.0
tilde-pkg = "~1.2.3"

# Greater than or equal
gte-pkg = ">=1.0.0"

# Range constraint
range-pkg = ">=1.0.0, <2.0.0"

# Pre-release versions
beta-pkg = "2.0.0-beta.1"
```

**Version Constraint Reference:**

| Constraint | Example | Matches | Doesn't Match |
|------------|---------|---------|---------------|
| Exact | `"1.2.3"` | 1.2.3 | 1.2.4, 1.3.0 |
| Caret | `"^1.2.3"` | 1.2.3, 1.9.9 | 2.0.0 |
| Tilde | `"~1.2.3"` | 1.2.3, 1.2.9 | 1.3.0 |
| GTE | `">=1.2.3"` | 1.2.3, 2.0.0, 5.0.0 | 1.2.2 |
| Range | `">=1.0, <2.0"` | 1.0.0, 1.9.9 | 2.0.0 |

**Key Concepts:**
- Use `^` for most dependencies (allows non-breaking updates)
- Use `~` when you need stability (only bug fixes)
- Use exact versions for critical dependencies
- Pre-release versions require explicit specification

---

### 18. Publishing a Package

**Difficulty:** Intermediate
**Time:** 10 minutes
**What you'll learn:** Package preparation, publishing workflow

```bash
# 1. Login to the registry
neam-pkg login
# Opens browser for authentication

# 2. Ensure your neam.toml is complete
# Required fields for publishing:
# - name, version, description, license

# 3. Build and validate
neam-pkg validate
# Checks for common issues

# 4. Publish to registry
neam-pkg publish

# 5. View your published package
neam-pkg info my-package
```

**Preparing a library package:**

```toml
# neam.toml for a library
neam_version = "1.0"

[project]
name = "rag-helpers"
version = "1.0.0"
description = "Utility functions for RAG pipelines"
type = "library"                    # Important: set to library
authors = ["Your Name <you@example.com>"]
license = "MIT"
keywords = ["rag", "retrieval", "utilities"]

[project.exports]
# What other packages can import
main = "src/lib.neam"

[dependencies]
# Keep minimal for libraries
```

**Library code (src/lib.neam):**

```neam
// src/lib.neam
// A reusable library for RAG operations

// Export functions for other packages to use
export function chunk_text(text, size, overlap) {
  // Implementation
  let chunks = [];
  // ... chunking logic
  return chunks;
}

export function merge_contexts(contexts) {
  let merged = "";
  for (ctx in contexts) {
    merged = merged + ctx + "\n\n";
  }
  return merged;
}

export knowledge DefaultKB {
  vector_store: "usearch",
  embedding_model: "nomic-embed-text",
  chunk_size: 256,
  chunk_overlap: 64
}
```

**Key Concepts:**
- `type: "library"` marks it as importable
- `exports` declares public API
- Keep library dependencies minimal
- Use clear, descriptive names and keywords

---

## Web API Deployment

### 19. Starting the API Server

**Difficulty:** Beginner
**Time:** 5 minutes
**What you'll learn:** Running neam-api, basic configuration

```bash
# Build the API server (if not already built)
cd build
cmake --build . --target neam-api --parallel

# Start with defaults (port 8080)
./neam-api

# Start with custom port
./neam-api --port 3000

# Start with custom host
./neam-api --host 127.0.0.1 --port 8080

# Show help
./neam-api --help
```

**Server output:**
```
Neam API Server v1.0.0
Listening on http://0.0.0.0:8080
Available endpoints:
  GET  /api/v1/health  - Health check
  GET  /api/v1/agents  - List agents
  POST /api/v1/agent/ask - Query an agent
```

**Key Concepts:**
- `neam-api` is the native HTTP server
- Binds to all interfaces (0.0.0.0) by default
- Agents are pre-configured in the server

---

### 20. Querying Agents via REST

**Difficulty:** Beginner
**Time:** 8 minutes
**What you'll learn:** HTTP endpoints, request/response format

```bash
# Health check
curl http://localhost:8080/api/v1/health

# Response:
# {"status": "healthy", "version": "1.0.0", "server": "neam-api"}

# List available agents
curl http://localhost:8080/api/v1/agents

# Response:
# {
#   "agents": [
#     {"id": "assistant", "description": "General purpose helpful assistant"},
#     {"id": "coder", "description": "Expert programmer"},
#     {"id": "analyst", "description": "Data analysis and insights"},
#     {"id": "writer", "description": "Creative writing"},
#     {"id": "researcher", "description": "Research with knowledge base", "has_rag": true}
#   ]
# }

# Query an agent
curl -X POST http://localhost:8080/api/v1/agent/ask \
  -H "Content-Type: application/json" \
  -d '{"agent_id": "assistant", "query": "What is the capital of France?"}'

# Response:
# {
#   "agent_id": "assistant",
#   "query": "What is the capital of France?",
#   "response": "The capital of France is Paris."
# }

# Query the coder agent
curl -X POST http://localhost:8080/api/v1/agent/ask \
  -H "Content-Type: application/json" \
  -d '{"agent_id": "coder", "query": "Write a Python function to check if a number is prime"}'
```

**Request Schema:**

```json
{
  "agent_id": "string (required)",
  "query": "string (required)",
  "context": "string (optional)",
  "temperature": "number (optional, 0.0-2.0)"
}
```

**Response Schema:**

```json
{
  "agent_id": "string",
  "query": "string",
  "response": "string",
  "tokens_used": "number (optional)",
  "latency_ms": "number (optional)"
}
```

**Key Concepts:**
- All endpoints under `/api/v1/`
- POST `/agent/ask` is the main query endpoint
- Responses are JSON formatted
- Context can be passed for follow-up queries

---

### 21. Custom Agent Endpoints

**Difficulty:** Intermediate
**Time:** 12 minutes
**What you'll learn:** Defining custom agents for API exposure

```neam
// api_agents.neam
// Define agents to be exposed via the API server

// General assistant
agent assistant {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "You are a helpful assistant. Provide clear, accurate answers.",
  temperature: 0.7
}

// Coding expert
agent coder {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "You are an expert programmer. Provide clean, well-documented
           code. Include examples when helpful. Support all major languages.",
  temperature: 0.3
}

// Data analyst
agent analyst {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "You analyze data and provide insights. Present findings
           clearly with supporting evidence. Use tables when helpful.",
  temperature: 0.5
}

// Creative writer
agent writer {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "You are a creative writer. Produce engaging, well-crafted
           content. Adapt your style to the request.",
  temperature: 0.9
}

// Customer support agent
agent support {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "You are a friendly customer support agent. Help users
           resolve issues. Be patient and thorough. Escalate when needed.",
  temperature: 0.5
}
```

**Querying custom agents:**

```bash
# Query the support agent
curl -X POST http://localhost:8080/api/v1/agent/ask \
  -H "Content-Type: application/json" \
  -d '{
    "agent_id": "support",
    "query": "I cannot log into my account. I keep getting an error.",
    "context": "User has been a customer for 2 years."
  }'
```

**Key Concepts:**
- Agent names become endpoint identifiers
- Temperature varies by use case (0.3 for code, 0.9 for creative)
- Context field allows passing conversation history or metadata

---

### 22. RAG-Enabled API Agents

**Difficulty:** Intermediate
**Time:** 12 minutes
**What you'll learn:** Exposing knowledge-connected agents via API

```neam
// rag_api_agents.neam
// Agents with knowledge bases for the API server

// Product documentation knowledge base
knowledge ProductKB {
  vector_store: "usearch",
  embedding_model: "nomic-embed-text",
  chunk_size: 256,
  chunk_overlap: 64,
  sources: [
    { type: "file", path: "./docs/products/*.md" }
  ],
  retrieval_strategy: "hybrid",
  top_k: 4
}

// Company policies knowledge base
knowledge PolicyKB {
  vector_store: "usearch",
  embedding_model: "nomic-embed-text",
  chunk_size: 300,
  chunk_overlap: 75,
  sources: [
    { type: "file", path: "./docs/policies/*.md" }
  ],
  retrieval_strategy: "self_rag",
  enable_relevance_check: true
}

// Technical documentation
knowledge TechKB {
  vector_store: "usearch",
  embedding_model: "nomic-embed-text",
  chunk_size: 350,
  chunk_overlap: 100,
  sources: [
    { type: "file", path: "./docs/technical/**/*.md" },
    { type: "web", url: "https://api.example.com/docs" }
  ],
  retrieval_strategy: "crag",
  enable_query_decomposition: true
}

// Product support agent with RAG
agent product_support {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "You are a product support specialist. Answer questions
           using only the provided product documentation. If the
           answer isn't in the docs, say so clearly.",
  connected_knowledge: [ProductKB],
  temperature: 0.3
}

// HR/Policy agent with RAG
agent hr_assistant {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "You help employees understand company policies.
           Only cite verified policy documents. Never make up policies.",
  connected_knowledge: [PolicyKB],
  temperature: 0.2
}

// Technical support with RAG
agent tech_support {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "You provide technical support using our documentation.
           Include code examples and step-by-step instructions.",
  connected_knowledge: [TechKB],
  temperature: 0.4
}
```

**Querying RAG-enabled agents:**

```bash
# Product question
curl -X POST http://localhost:8080/api/v1/agent/ask \
  -H "Content-Type: application/json" \
  -d '{
    "agent_id": "product_support",
    "query": "What are the system requirements for ProductX?"
  }'

# Policy question
curl -X POST http://localhost:8080/api/v1/agent/ask \
  -H "Content-Type: application/json" \
  -d '{
    "agent_id": "hr_assistant",
    "query": "What is the policy on remote work?"
  }'

# Technical question
curl -X POST http://localhost:8080/api/v1/agent/ask \
  -H "Content-Type: application/json" \
  -d '{
    "agent_id": "tech_support",
    "query": "How do I configure OAuth2 authentication?"
  }'
```

**Response with RAG context:**

```json
{
  "agent_id": "tech_support",
  "query": "How do I configure OAuth2 authentication?",
  "response": "To configure OAuth2 authentication:\n\n1. Register your app...",
  "sources": [
    {"file": "docs/technical/auth/oauth2.md", "relevance": 0.94},
    {"file": "docs/technical/auth/tokens.md", "relevance": 0.87}
  ]
}
```

**Key Concepts:**
- Agents connect to knowledge bases via `connected_knowledge`
- Different RAG strategies for different use cases
- API responses can include source citations
- Lower temperature for factual accuracy

---

### 23. Production Deployment

**Difficulty:** Advanced
**Time:** 15 minutes
**What you'll learn:** Docker deployment, environment configuration, scaling

**Dockerfile:**

```dockerfile
# Dockerfile for neam-api
FROM ubuntu:22.04 AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    cmake \
    g++ \
    git \
    libcurl4-openssl-dev \
    && rm -rf /var/lib/apt/lists/*

# Copy source
WORKDIR /app
COPY . .

# Build
RUN mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    cmake --build . --target neam-api --parallel

# Runtime image
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    libcurl4 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy binary and docs
COPY --from=builder /app/build/neam-api .
COPY docs/ ./docs/

# Create non-root user
RUN useradd -m neam && chown -R neam:neam /app
USER neam

EXPOSE 8080

ENTRYPOINT ["./neam-api"]
CMD ["--port", "8080"]
```

**docker-compose.yml:**

```yaml
version: '3.8'

services:
  neam-api:
    build: .
    ports:
      - "8080:8080"
    environment:
      - OPENAI_API_KEY=${OPENAI_API_KEY}
      - NEAM_LOG_LEVEL=info
      - NEAM_MAX_TOKENS=4096
    volumes:
      - ./docs:/app/docs:ro
    healthcheck:
      test: ["CMD", "curl", "-f", "http://localhost:8080/api/v1/health"]
      interval: 30s
      timeout: 10s
      retries: 3
    restart: unless-stopped
    deploy:
      resources:
        limits:
          cpus: '2'
          memory: 4G
```

**Environment variables:**

```bash
# .env file
OPENAI_API_KEY=sk-your-api-key
ANTHROPIC_API_KEY=sk-ant-your-key
OLLAMA_HOST=http://localhost:11434

# Neam configuration
NEAM_LOG_LEVEL=info         # debug, info, warn, error
NEAM_MAX_TOKENS=4096        # Maximum response tokens
NEAM_REQUEST_TIMEOUT=30000  # Request timeout in ms
NEAM_RATE_LIMIT=100         # Requests per minute
```

**Running in production:**

```bash
# Build and start
docker-compose up -d

# View logs
docker-compose logs -f neam-api

# Scale horizontally
docker-compose up -d --scale neam-api=3

# Health check
curl http://localhost:8080/api/v1/health
```

**Load balancer (nginx):**

```nginx
# nginx.conf
upstream neam_api {
    least_conn;
    server neam-api-1:8080;
    server neam-api-2:8080;
    server neam-api-3:8080;
}

server {
    listen 80;
    server_name api.example.com;

    location /api/ {
        proxy_pass http://neam_api;
        proxy_http_version 1.1;
        proxy_set_header Connection "";
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_connect_timeout 60s;
        proxy_read_timeout 120s;
    }
}
```

**Key Concepts:**
- Multi-stage Docker build for smaller images
- Environment variables for configuration
- Health checks for orchestration
- Horizontal scaling with load balancer
- Non-root user for security

---

## Quick Reference

### RAG Strategy Selection Guide

```
Question Type                    → Recommended Strategy
─────────────────────────────────────────────────────
Simple factual Q&A              → basic
Need diverse perspectives       → mmr
Has specific codes/IDs          → hybrid
Abstract/conceptual             → hyde
High-stakes (legal, medical)    → self_rag
Multi-part comparison           → crag
Deep research                   → agentic
```

### Package Manager Cheatsheet

```bash
neam-pkg init <name>              # Create project
neam-pkg install                  # Install from neam.toml
neam-pkg install <pkg>            # Add package
neam-pkg install <pkg>@<ver>      # Specific version
neam-pkg install --dev <pkg>      # Dev dependency
neam-pkg update                   # Update all
neam-pkg remove <pkg>             # Remove package
neam-pkg list                     # List installed
neam-pkg outdated                 # Check updates
neam-pkg search <query>           # Find packages
neam-pkg publish                  # Publish to registry
```

### API Server Quick Reference

```bash
# Start server
./neam-api --port 8080

# Health check
curl http://localhost:8080/api/v1/health

# List agents
curl http://localhost:8080/api/v1/agents

# Query agent
curl -X POST http://localhost:8080/api/v1/agent/ask \
  -H "Content-Type: application/json" \
  -d '{"agent_id": "assistant", "query": "Hello!"}'
```

---

## Next Steps

Congratulations! You've completed Learn Neam Part 2. You now understand:

- **Special Agent Patterns**: Chain-of-thought, planning, Socratic teaching, security analysis
- **All 7 RAG Strategies**: When and how to use each one
- **Project Management**: Creating, configuring, and publishing packages
- **API Deployment**: Running agents as production web services

### Continue Learning

1. **Build a project**: Start with `neam-pkg init my-project`
2. **Experiment with RAG**: Try different strategies on your data
3. **Deploy an API**: Containerize and deploy your agents
4. **Join the community**: Share your patterns and packages

### Resources

- [Neam Language Reference](./LANGUAGE_REFERENCE.md)
- [API Documentation](./API_REFERENCE.md)
- [Package Registry](https://registry.neam.dev)
- [Community Examples](https://github.com/neam-lang/examples)
