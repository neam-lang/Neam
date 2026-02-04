"""
Neam Runner - Agent loop execution.

This module provides the Runner class for executing agent loops
with handoffs and tracing.
"""

from __future__ import annotations

import json
from typing import Any, Callable, Dict, List, Optional, TYPE_CHECKING

from . import (
    _lib,
    _NeamRunner,
    _NeamTask,
    _encode,
    _decode,
    TaskStatus,
)
from .runtime import Result

if TYPE_CHECKING:
    from .runtime import Runtime


class Runner:
    """
    Executes an agent loop with automatic handoffs.

    A Runner manages the execution flow when multiple agents can
    hand off to each other. It starts with an entry agent and
    continues until the conversation is complete or max_turns
    is reached.

    Example:
        >>> runner = Runner(runtime, "triage_agent", max_turns=10)
        >>> runner.enable_tracing(True)
        >>> result = runner.run("I need help with my order")
        >>> print(f"Final agent: {runner.final_agent}")
        >>> print(f"Total turns: {runner.total_turns}")

    Args:
        runtime: The Neam runtime
        entry_agent: Name of the starting agent
        max_turns: Maximum number of turns before stopping
    """

    def __init__(
        self,
        runtime: "Runtime",
        entry_agent: str,
        max_turns: int = 10,
    ):
        self._runtime = runtime
        self._entry_agent = entry_agent
        self._handle = _lib.neam_runner_create(
            runtime._handle,
            _encode(entry_agent),
            max_turns,
        )

        if not self._handle:
            raise RuntimeError("Failed to create runner")

        self._trace_callback = None

    def __del__(self):
        if self._handle:
            _lib.neam_runner_free(self._handle)
            self._handle = None

    def enable_tracing(self, enabled: bool = True) -> None:
        """
        Enable or disable tracing.

        When tracing is enabled, the runner will record each step
        of the agent loop for debugging and analysis.

        Args:
            enabled: True to enable tracing
        """
        _lib.neam_runner_enable_tracing(self._handle, 1 if enabled else 0)

    def run(self, input_text: str) -> Result:
        """
        Run the agent loop with the given input.

        Args:
            input_text: The initial user input

        Returns:
            Result containing the final output

        Example:
            >>> result = runner.run("What is my account balance?")
            >>> if not result.is_error:
            ...     print(result.output)
        """
        handle = _lib.neam_runner_run(self._handle, _encode(input_text))
        return Result(handle)

    @property
    def final_agent(self) -> str:
        """Get the name of the final agent after a run."""
        return _decode(_lib.neam_runner_final_agent(self._handle)) or ""

    @property
    def total_turns(self) -> int:
        """Get the total number of turns used in the last run."""
        return _lib.neam_runner_total_turns(self._handle)

    def __repr__(self) -> str:
        return f"Runner(entry_agent={self._entry_agent!r})"


class Task:
    """
    Represents an A2A Protocol task.

    Tasks allow asynchronous execution of agent work with
    status tracking and result retrieval.

    Example:
        >>> task = Task(runtime, "refund_agent", {"order_id": "123"})
        >>> task.submit()
        >>> task.wait(timeout_ms=30000)
        >>> if task.status == TaskStatus.COMPLETED:
        ...     print(task.result)

    Args:
        runtime: The Neam runtime
        agent_name: Target agent name
        input_data: Input data as a dictionary
    """

    def __init__(
        self,
        runtime: "Runtime",
        agent_name: str,
        input_data: Dict[str, Any],
    ):
        self._runtime = runtime
        self._agent_name = agent_name
        input_json = json.dumps(input_data)
        self._handle = _lib.neam_task_create(
            runtime._handle,
            _encode(agent_name),
            _encode(input_json),
        )

        if not self._handle:
            raise RuntimeError("Failed to create task")

    def __del__(self):
        if self._handle:
            _lib.neam_task_free(self._handle)
            self._handle = None

    @property
    def id(self) -> str:
        """Get the task ID."""
        return _decode(_lib.neam_task_id(self._handle)) or ""

    @property
    def status(self) -> int:
        """Get the current task status."""
        return _lib.neam_task_status(self._handle)

    @property
    def status_name(self) -> str:
        """Get the status as a string."""
        status_map = {
            TaskStatus.PENDING: "pending",
            TaskStatus.RUNNING: "running",
            TaskStatus.COMPLETED: "completed",
            TaskStatus.FAILED: "failed",
            TaskStatus.CANCELLED: "cancelled",
        }
        return status_map.get(self.status, "unknown")

    def submit(self) -> None:
        """Submit the task for execution."""
        result = _lib.neam_task_submit(self._handle)
        if result != 0:
            raise RuntimeError("Failed to submit task")

    def wait(self, timeout_ms: int = -1) -> bool:
        """
        Wait for task completion.

        Args:
            timeout_ms: Timeout in milliseconds (-1 for infinite)

        Returns:
            True if completed, False if timeout or error
        """
        result = _lib.neam_task_wait(self._handle, timeout_ms)
        return result == 0

    @property
    def result(self) -> Optional[Dict[str, Any]]:
        """
        Get the task result.

        Returns:
            Result dictionary if completed, None otherwise
        """
        result_json = _lib.neam_task_result(self._handle)
        if result_json:
            result_str = _decode(result_json)
            _lib.neam_string_free(result_json)
            if result_str:
                return json.loads(result_str)
        return None

    def cancel(self) -> None:
        """Cancel the task."""
        result = _lib.neam_task_cancel(self._handle)
        if result != 0:
            raise RuntimeError("Failed to cancel task")

    def __repr__(self) -> str:
        return f"Task(id={self.id!r}, status={self.status_name!r})"
