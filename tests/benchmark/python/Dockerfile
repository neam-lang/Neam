FROM python:3.12-slim

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY bedrock_agent.py .
COPY ../prompts.jsonl ./prompts.jsonl

ENV PROMPTS_FILE=/app/prompts.jsonl

CMD ["python", "bedrock_agent.py"]
