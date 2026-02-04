"""
Neam Agent - AI agent management.

This module provides the Agent class for creating and interacting with
AI agents in Neam.
"""

from __future__ import annotations

from typing import Any, Dict, List, Optional, TYPE_CHECKING

from . import (
    _lib,
    _NeamAgent,
    _encode,
    _decode,
)

if TYPE_CHECKING:
    from .runtime import Runtime


class Agent:
    """
    Represents a Neam AI agent.

    Agents are the core abstraction for interacting with LLMs in Neam.
    They can have a system prompt, connected knowledge, tools, and
    support handoffs to other agents.

    Example:
        >>> runtime = Runtime()
        >>> agent = Agent(
        ...     runtime,
        ...     "assistant",
        ...     provider="ollama",
        ...     model="llama3.2:3b",
        ...     system="You are a helpful assistant."
        ... )
        >>> response = agent.ask("What is 2 + 2?")
        >>> print(response)

    Args:
        runtime: The Neam runtime instance
        name: Agent name
        provider: LLM provider (e.g., "ollama", "openai")
        model: Model name
        system: System prompt
    """

    def __init__(
        self,
        runtime: "Runtime",
        name: str,
        provider: str = "ollama",
        model: str = "llama3.2:3b",
        system: str = "",
    ):
        self._runtime = runtime
        self._name = name
        self._handle = _lib.neam_agent_create(
            runtime._handle,
            _encode(name),
            _encode(provider),
            _encode(model),
            _encode(system),
        )

        if not self._handle:
            raise RuntimeError(f"Failed to create agent '{name}'")

    def __del__(self):
        if self._handle:
            _lib.neam_agent_free(self._handle)
            self._handle = None

    @property
    def name(self) -> str:
        """Get the agent name."""
        return self._name

    def set_temperature(self, temperature: float) -> None:
        """
        Set the agent's temperature.

        Args:
            temperature: Temperature value (0.0 to 2.0)
        """
        _lib.neam_agent_set_temperature(self._handle, temperature)

    def set_context_from(self, path: str) -> None:
        """
        Set agent context from a file or directory.

        Args:
            path: Path to context file or directory
        """
        result = _lib.neam_agent_set_context_from(self._handle, _encode(path))
        if result != 0:
            raise RuntimeError(f"Failed to set context from '{path}'")

    def ask(self, query: str) -> str:
        """
        Send a query to the agent and get a response.

        Args:
            query: The question or request

        Returns:
            The agent's response

        Example:
            >>> response = agent.ask("Explain quantum computing")
            >>> print(response)
        """
        response = _lib.neam_agent_ask(self._handle, _encode(query))
        result = _decode(response) or ""
        if response:
            _lib.neam_string_free(response)
        return result

    def add_handoff(
        self,
        target: str,
        tool_name: Optional[str] = None,
        description: str = "",
    ) -> None:
        """
        Add a handoff target to this agent.

        Handoffs allow an agent to transfer control to another agent
        during a conversation.

        Args:
            target: Target agent name
            tool_name: Custom tool name for the handoff (optional)
            description: Description of when to use this handoff

        Example:
            >>> triage_agent.add_handoff(
            ...     "billing_agent",
            ...     description="Transfer to billing for payment issues"
            ... )
        """
        result = _lib.neam_agent_add_handoff(
            self._handle,
            _encode(target),
            _encode(tool_name) if tool_name else None,
            _encode(description),
        )
        if result != 0:
            raise RuntimeError(f"Failed to add handoff to '{target}'")

    def reset(self) -> None:
        """Reset the agent's conversation history."""
        _lib.neam_agent_reset(self._handle)

    def __repr__(self) -> str:
        return f"Agent(name={self._name!r})"


def get_agent_card(runtime: "Runtime", agent_name: str) -> Optional[Dict[str, Any]]:
    """
    Get an agent's card as a dictionary (A2A Protocol).

    Args:
        runtime: The Neam runtime
        agent_name: Agent name

    Returns:
        Agent card dictionary, or None if not found
    """
    import json
    card_json = _lib.neam_get_agent_card(runtime._handle, _encode(agent_name))
    if card_json:
        result = _decode(card_json)
        _lib.neam_string_free(card_json)
        if result:
            return json.loads(result)
    return None


def register_agent_card(
    runtime: "Runtime",
    agent_name: str,
    card: Dict[str, Any],
) -> None:
    """
    Register an agent card (A2A Protocol).

    Args:
        runtime: The Neam runtime
        agent_name: Agent name
        card: Agent card dictionary

    Example:
        >>> register_agent_card(runtime, "my_agent", {
        ...     "version": "1.0.0",
        ...     "description": "A helpful assistant",
        ...     "capabilities": ["chat", "code-review"],
        ...     "input_schema": {"query": {"type": "string"}},
        ...     "output_schema": {"response": {"type": "string"}}
        ... })
    """
    import json
    card_json = json.dumps(card)
    result = _lib.neam_register_agent_card(
        runtime._handle,
        _encode(agent_name),
        _encode(card_json),
    )
    if result != 0:
        raise RuntimeError(f"Failed to register agent card for '{agent_name}'")
