#!/usr/bin/env bash

# ============================================
# Ralphy - Autonomous AI Coding Loop
# Supports Claude Code, OpenCode, Codex, Cursor, and g3
# Runs until PRD is complete
# ============================================

set -euo pipefail

if [[ -t 1 ]] && command -v tput &>/dev/null && [[ $(tput colors 2>/dev/null || echo 0) -ge 8 ]]; then
  RED=$(tput setaf 1)
  GREEN=$(tput setaf 2)
  YELLOW=$(tput setaf 3)
  BLUE=$(tput setaf 4)
  MAGENTA=$(tput setaf 5)
  CYAN=$(tput setaf 6)
  BOLD=$(tput bold)
  DIM=$(tput dim)
  RESET=$(tput sgr0)
else
  RED="" GREEN="" YELLOW="" BLUE="" MAGENTA="" CYAN="" BOLD="" DIM="" RESET=""
fi

# Configuration
AI_ENGINE="claude"  # claude, opencode, cursor, codex, or g3
MAX_ITERATIONS=0  # 0 = unlimited
MAX_RETRIES=3
SKIP_TESTS=false
SKIP_LINT=false
VERBOSE=false
DRY_RUN=false
PARALLEL=false
MAX_PARALLEL=3
BRANCH_PER_TASK=false
CREATE_PR=false
PRD_FILE="PRD.md"
PRD_SOURCE="markdown"
GITHUB_LABEL=""
LOG_DIR="ralphy_logs"

# Global state
ai_pid=""
monitor_pid=""
tmpfile=""
CODEX_LAST_MESSAGE_FILE=""
current_step="Thinking"
total_input_tokens=0
total_output_tokens=0
total_actual_cost="0"  # OpenCode provides actual cost
total_duration_ms=0    # Cursor provides duration
iteration=0
retry_count=0
declare -a parallel_pids=()
declare -a task_branches=()
WORKTREE_BASE=""  # Base directory for parallel agent worktrees
ORIGINAL_DIR=""   # Original working directory (for worktree operations)
GITHUB_REPO=""

# ============================================
# UTILITY FUNCTIONS
# ============================================

log_info() {
  echo "${BLUE}[INFO]${RESET} $*"
}

log_success() {
  echo "${GREEN}[OK]${RESET} $*"
}

log_warn() {
  echo "${YELLOW}[WARN]${RESET} $*"
}

log_error() {
  echo "${RED}[ERROR]${RESET} $*" >&2
}

log_debug() {
  if [[ "$VERBOSE" == true ]]; then
    echo "${DIM}[DEBUG] $*${RESET}"
  fi
}

calculate_cost() {
  local input=$1
  local output=$2
  
  if command -v bc &>/dev/null; then
    # Claude 3.5 Sonnet costs (approximate)
    # Input: $3/M tokens, Output: $15/M tokens
    local input_cost
    local output_cost
    input_cost=$(echo "scale=6; $input * 3 / 1000000" | bc)
    output_cost=$(echo "scale=6; $output * 15 / 1000000" | bc)
    echo "scale=4; $input_cost + $output_cost" | bc
  else
    echo "N/A (install 'bc')"
  fi
}

spinner() {
  local pid=$1
  local delay=0.1
  local spinstr='|/-\'
  local dot_delay=0.5
  local last_dot_time=0
  local dots=""
  
  tput civis # Hide cursor
  
  while kill -0 "$pid" 2>/dev/null; do
    local temp=${spinstr#?}
    printf " [%c]  " "$spinstr"
    local spinstr=$temp${spinstr%"$temp"}
    
    # Check if we should update dots (every 0.5s)
    local current_time
    if [[ "$OSTYPE" == "darwin"* ]]; then
      current_time=$(date +%s)  # macOS date doesn't support %N easily
    else
      current_time=$(date +%s.%N)
    fi
    
    # Calculate time diff (handling floating point)
    local diff
    if command -v bc &>/dev/null; then
       diff=$(echo "$current_time - $last_dot_time" | bc)
       is_time=$(echo "$diff >= $dot_delay" | bc)
    else
       # Fallback for integer math if bc missing
       diff=$(( ${current_time%.*} - ${last_dot_time%.*} ))
       if [[ $diff -ge 1 ]]; then is_time=1; else is_time=0; fi
    fi

    if [[ "$is_time" -eq 1 ]]; then
      dots="${dots}."
      if [[ ${#dots} -gt 3 ]]; then dots=""; fi
      last_dot_time=$current_time
    fi
    
    printf "%-20s" "${current_step}${dots}"
    printf "\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b"
    sleep $delay
  done
  
  printf "    \b\b\b\b"
  tput cnorm # Show cursor
}

# ============================================
# PARSE ARGUMENTS
# ============================================

show_help() {
  echo "Usage: ralphy [OPTIONS]"
  echo ""
  echo "Options:"
  echo "  --g3                Use Google g3 (Local Agent)"
  echo "  --claude            Use Claude Code (default)"
  echo "  --opencode          Use OpenCode CLI"
  echo "  --cursor            Use Cursor Agent"
  echo "  --codex             Use Codex CLI"
  echo "  --max N             Run N iterations then stop"
  echo "  --prd FILE          Specify PRD markdown file (default: PRD.md)"
  echo "  --yaml FILE         Specify YAML task file"
  echo "  --github OWNER/REPO Use GitHub Issues as task source"
  echo "  --label LABEL       Filter GitHub issues by label"
  echo "  --fast              Skip tests and linting"
  echo "  --verbose           Show debug logs"
  echo "  --dry-run           Show tasks without running agent"
  echo "  --parallel [N]      Run tasks in parallel (experimental)"
  echo "  --branch-per-task   Create a git branch for each task"
  echo "  --create-pr         Create a GitHub PR after task completion"
  echo "  --help              Show this help"
  echo ""
  exit 0
}

parse_args() {
  while [[ $# -gt 0 ]]; do
    case $1 in
      --g3)
        AI_ENGINE="g3"
        shift
        ;;
      --claude)
        AI_ENGINE="claude"
        shift
        ;;
      --opencode)
        AI_ENGINE="opencode"
        shift
        ;;
      --cursor)
        AI_ENGINE="cursor"
        shift
        ;;
      --codex)
        AI_ENGINE="codex"
        shift
        ;;
      --max)
        MAX_ITERATIONS="$2"
        shift 2
        ;;
      --prd)
        PRD_FILE="$2"
        PRD_SOURCE="markdown"
        shift 2
        ;;
      --yaml)
        PRD_FILE="$2"
        PRD_SOURCE="yaml"
        shift 2
        ;;
      --github)
        GITHUB_REPO="$2"
        PRD_SOURCE="github"
        shift 2
        ;;
      --label)
        GITHUB_LABEL="$2"
        shift 2
        ;;
      --fast)
        SKIP_TESTS=true
        SKIP_LINT=true
        shift
        ;;
      --verbose)
        VERBOSE=true
        shift
        ;;
      --dry-run)
        DRY_RUN=true
        shift
        ;;
      --parallel)
        PARALLEL=true
        if [[ $# -gt 1 ]] && [[ "$2" =~ ^[0-9]+$ ]]; then
          MAX_PARALLEL="$2"
          shift 2
        else
          shift
        fi
        ;;
      --branch-per-task)
        BRANCH_PER_TASK=true
        shift
        ;;
      --create-pr)
        CREATE_PR=true
        shift
        ;;
      --help)
        show_help
        ;;
      *)
        log_error "Unknown option: $1"
        show_help
        ;;
    esac
  done
}

# ============================================
# CHECK REQUIREMENTS
# ============================================

check_requirements() {
  local missing=()
  
  case "$AI_ENGINE" in
    g3)
      if ! command -v g3 &>/dev/null; then
        log_error "g3 CLI not found."
        exit 1
      fi
      ;;
    claude)
      if ! command -v claude &>/dev/null; then
        log_error "Claude Code CLI not found. Install from https://github.com/anthropics/claude-code"
        log_warn "Token tracking may not work properly"
      fi
      ;;
    opencode)
      if ! command -v opencode &>/dev/null; then
        log_error "OpenCode CLI not found. Install from https://opencode.ai/docs/"
        exit 1
      fi
      ;;
    codex)
      if ! command -v codex &>/dev/null; then
        log_error "Codex CLI not found. Make sure 'codex' is in your PATH."
        exit 1
      fi
      ;;
    cursor)
      if ! command -v cursor &>/dev/null; then
        log_error "Cursor CLI not found. Make sure 'cursor' is in your PATH."
        exit 1
      fi
      ;;
  esac

  if ! command -v jq &>/dev/null; then
    missing+=("jq")
  fi
  
  if [[ "$PRD_SOURCE" == "yaml" ]] && ! command -v yq &>/dev/null; then
    missing+=("yq")
  fi
  
  if [[ "$PRD_SOURCE" == "github" ]] && ! command -v gh &>/dev/null; then
    missing+=("gh")
    log_error "GitHub CLI (gh) is required. Install from https://cli.github.com/"
    exit 1
  fi
  
  if [[ "$CREATE_PR" == true ]] && ! command -v gh &>/dev/null; then
    log_error "GitHub CLI (gh) is required for --create-pr. Install from https://cli.github.com/"
    exit 1
  fi

  if [[ ${#missing[@]} -gt 0 ]]; then
    log_warn "Missing optional dependencies: ${missing[*]}"
  fi
  
  # Create progress.txt if missing
  if [[ ! -f "progress.txt" ]]; then
    log_warn "progress.txt not found, creating it..."
    touch progress.txt
  fi
  
  # Create logs directory
  mkdir -p "$LOG_DIR"
  
  # Store original directory
  ORIGINAL_DIR=$(pwd)
}

# ============================================
# AGENT EXECUTION
# ============================================

run_agent() {
  local prompt="$1"
  local log_file="$LOG_DIR/agent_$(date +%s).log"
  local task_id="${2-}"
  
  log_debug "Starting AI agent with engine: $AI_ENGINE"
  current_step="Thinking"
  
  # Clean temp file
  tmpfile=$(mktemp)
  
  # Start spinner in background
  (spinner $$) &
  monitor_pid=$!
  
  # Run selected AI engine
  case "$AI_ENGINE" in
    g3)
        # g3 execution
        g3 "$prompt" > "$tmpfile" 2>&1
        ;;
    claude)
      claude --dangerously-skip-permissions \
        --verbose \
        --print \
        "$prompt" 2>&1 | tee "$tmpfile"
      ;;
      
    opencode)
      # Set permissions to allow everything
      export OPENCODE_PERMISSION='{"*":"allow"}'
      opencode -p "$prompt" --auto --json > "$tmpfile" 2>&1
      ;;
      
    cursor)
      # Cursor agent (experimental)
      # Note: Cursor CLI args are tricky, assuming 'cursor --agent' or similar alias exists
      # You might need to alias 'cursor' to the actual executable path
      cursor agent "$prompt" > "$tmpfile" 2>&1
      ;;
      
    codex)
      if [[ -z "$CODEX_LAST_MESSAGE_FILE" ]]; then
        CODEX_LAST_MESSAGE_FILE="$tmpfile.last"
      fi
      
      codex exec --full-auto \
            --output-last-message "$CODEX_LAST_MESSAGE_FILE" \
            --output-format stream-json \
            "$prompt" 2>&1 > "$tmpfile"
      ;;
  esac
  
  # Capture exit code
  local exit_code=$?
  
  # Stop spinner
  kill "$monitor_pid" 2>/dev/null || true
  wait "$monitor_pid" 2>/dev/null || true
  printf "\n"
  
  # Process output for stats
  extract_stats "$tmpfile"
  
  # Log output
  cat "$tmpfile" >> "$log_file"
  
  # Return result
  cat "$tmpfile"
  rm -f "$tmpfile"
  
  return $exit_code
}

check_for_errors() {
  local output="$1"
  if [[ "$output" == *"Error:"* ]] || [[ "$output" == *"Exception:"* ]]; then
    return 0 # Found errors
  else
    return 1 # No errors
  fi
}

extract_stats() {
  local file="$1"
  local content
  content=$(cat "$file")
  
  if [[ "$AI_ENGINE" == "claude" ]]; then
    # Try to parse Claude output for token usage if available
    # This relies on verbose output format
    local in_tok
    local out_tok
    in_tok=$(echo "$content" | grep -o "input_tokens: [0-9]*" | grep -o "[0-9]*" | tail -1 || echo "0")
    out_tok=$(echo "$content" | grep -o "output_tokens: [0-9]*" | grep -o "[0-9]*" | tail -1 || echo "0")
    
    [[ "$in_tok" =~ ^[0-9]+$ ]] && total_input_tokens=$((total_input_tokens + in_tok))
    [[ "$out_tok" =~ ^[0-9]+$ ]] && total_output_tokens=$((total_output_tokens + out_tok))
    
  elif [[ "$AI_ENGINE" == "opencode" ]]; then
    # OpenCode provides JSON output with cost
    local json
    json=$(echo "$content" | grep "^{" | tail -1 || echo "{}")
    local cost
    cost=$(echo "$json" | jq -r '.cost // 0' 2>/dev/null || echo "0")
    
    # Add to total cost (requires floating point math support)
    if command -v bc &>/dev/null; then
        total_actual_cost=$(echo "$total_actual_cost + $cost" | bc)
    fi
  
  elif [[ "$AI_ENGINE" == "cursor" ]]; then
    # Cursor doesn't provide token usage, but does provide duration
    local dur_ms
    dur_ms=$(echo "$content" | grep -o "duration: [0-9]*ms" | grep -o "[0-9]*" | tail -1 || echo "0")
    [[ "$dur_ms" =~ ^[0-9]+$ ]] && total_duration_ms=$((total_duration_ms + dur_ms))
  fi
}

# ============================================
# TASK MANAGEMENT
# ============================================

get_next_task_markdown() {
  grep -n "\- \[ \]" "$PRD_FILE" | head -1 | sed 's/^- \[ \] //'
}

get_next_task_yaml() {
  yq eval '.tasks[] | select(.completed != true) | .title' "$PRD_FILE" | head -1
}

get_next_task_github() {
  local args=("--repo" "$GITHUB_REPO" "--state" "open" "--limit" "1" "--json" "number,title")
  if [[ -n "$GITHUB_LABEL" ]]; then
    args+=("--label" "$GITHUB_LABEL")
  fi
  gh issue list "${args[@]}" | jq -r '.[0] | "\(.number):\(.title)"'
}

get_next_task() {
  case "$PRD_SOURCE" in
    markdown) get_next_task_markdown ;;
    yaml) get_next_task_yaml ;;
    github) get_next_task_github ;;
  esac
}

count_remaining_markdown() {
  grep -c "\- \[ \]" "$PRD_FILE" || echo "0"
}

count_remaining_yaml() {
  yq eval '[.tasks[] | select(.completed != true)] | length' "$PRD_FILE" 2>/dev/null || echo "0"
}

count_remaining_github() {
  local args=("--repo" "$GITHUB_REPO" "--state" "open" "--json" "number")
  if [[ -n "$GITHUB_LABEL" ]]; then
    args+=("--label" "$GITHUB_LABEL")
  fi
  gh issue list "${args[@]}" | jq 'length'
}

count_remaining_tasks() {
  case "$PRD_SOURCE" in
    markdown) count_remaining_markdown ;;
    yaml) count_remaining_yaml ;;
    github) count_remaining_github ;;
  esac
}

mark_task_complete_markdown() {
  local task="$1"
  # This is tricky with sed safely, simplified approach:
  # Find the first unchecked box and check it
  # Ideally we'd match the specific task text
  
  # Escape special chars for sed
  local task_escaped
  task_escaped=$(echo "$task" | sed 's/[\/&]/\\&/g')
  
  # For line number approach (from get_next_task_markdown)
  if [[ "$task" =~ ^[0-9]+: ]]; then
    local line_num=${task%%:*}
    if [[ "$OSTYPE" == "darwin"* ]]; then
      sed -i '' "${line_num}s/- \[ \]/- [x]/" "$PRD_FILE"
    else
      sed -i "${line_num}s/- \[ \]/- [x]/" "$PRD_FILE"
    fi
  else
    # First available task approach
    if [[ "$OSTYPE" == "darwin"* ]]; then
      sed -i '' "0,/- \[ \]/s//- [x]/" "$PRD_FILE"
    else
      sed -i "0,/- \[ \]/s//- [x]/" "$PRD_FILE"
    fi
  fi
}

mark_task_complete_yaml() {
  local task="$1"
  # yq update
  yq eval -i "(.tasks[] | select(.title == \"$task\")).completed = true" "$PRD_FILE"
}

mark_task_complete_github() {
  local task="$1"
  # Extract issue number from "number:title" format
  if [[ "$task" =~ ^([0-9]+): ]]; then
    local issue_num="${BASH_REMATCH[1]}"
    gh issue close "$issue_num" --repo "$GITHUB_REPO" --comment "Completed by Ralphy"
  else
    log_error "Could not parse issue number from task: $task"
  fi
}

mark_task_complete() {
  case "$PRD_SOURCE" in
    markdown) mark_task_complete_markdown "$1" ;;
    yaml) mark_task_complete_yaml "$1" ;;
    github) mark_task_complete_github "$1" ;;
  esac
}

# ============================================
# EXECUTION LOOP
# ============================================

run_single_task() {
  local task_override="${1-}"
  local iter="${2-}"
  
  local task
  if [[ -n "$task_override" ]]; then
    task="$task_override"
  else
    # Fetch next task
    task_raw=$(get_next_task)
    
    # Handle markdown line numbers
    if [[ "$PRD_SOURCE" == "markdown" ]]; then
       task=$(echo "$task_raw" | cut -d: -f2- | sed 's/^ *//')
       task_line=$(echo "$task_raw" | cut -d: -f1)
       task_ref="${task_line}:${task}" # Pass line number for accurate marking
    else
       task="$task_raw"
       task_ref="$task"
    fi
  fi

  if [[ -z "$task" ]] || [[ "$task" == "null" ]]; then
    return 2 # No more tasks
  fi

  echo "${BOLD}Task #$iter:${RESET} $task"

  # Construct Prompt
  local prompt="You are an autonomous coding agent.
Your goal is to complete the following task:
$task

Context:
This task is part of a larger project defined in $PRD_FILE.
Refer to previous work if needed.

Instructions:
1. Understand the task requirements.
2. Explore the codebase if necessary.
3. Write the code to implement the feature or fix.
4. Verify your changes (run tests or create a verification script).
5. Only when you are confident it works, report completion.

IMPORTANT: You are running in a loop. Do not ask for user permission. Just do the work.
"

  # Run Agent
  local response
  response=$(run_agent "$prompt")
  local exit_code=$?
  
  if [[ $exit_code -ne 0 ]]; then
    log_error "Agent failed with exit code $exit_code"
    ((retry_count++))
    if [[ $retry_count -ge $MAX_RETRIES ]]; then
      log_error "Max retries reached for this task."
      return 1
    fi
    return 0 # Retry same task
  fi
  
  # Check output for completion signal or success
  # For now, assume if agent exits 0, it thinks it's done. 
  # In a real agent loop, we'd parse "I have completed the task"
  
  log_success "Task completed."
  mark_task_complete "$task_ref"
  
  # Reset retry count
  retry_count=0
  
  return 0
}

# ============================================
# SUMMARY
# ============================================

show_summary() {
  echo ""
  echo "${BOLD}============================================${RESET}"
  echo "${GREEN}PRD complete!${RESET} Finished $iteration task(s)."
  echo "${BOLD}============================================${RESET}"
  echo ""
  echo "${BOLD}>>> Cost Summary${RESET}"
  
  echo "Input tokens:  $total_input_tokens"
  echo "Output tokens: $total_output_tokens"
  echo "Total tokens:  $((total_input_tokens + total_output_tokens))"
  
  if [[ "$AI_ENGINE" == "opencode" ]]; then
      echo "Actual cost:   \$${total_actual_cost}"
  else
      local cost
      cost=$(calculate_cost "$total_input_tokens" "$total_output_tokens")
      echo "Est. cost:     \$$cost"
  fi
  
  echo "${BOLD}============================================${RESET}"
}

notify_done() {
  if [[ "$OSTYPE" == "darwin"* ]]; then
    osascript -e "display notification \"${1:-Ralphy finished all tasks}\" with title \"Ralphy\"" 2>/dev/null || true
  fi
}

# ============================================
# MAIN
# ============================================

main() {
  parse_args "$@"
  
  if [[ "$DRY_RUN" == true ]] && [[ "$MAX_ITERATIONS" -eq 0 ]]; then
    MAX_ITERATIONS=1
  fi

  # Set up cleanup trap
  # trap cleanup EXIT (not implemented fully yet)
  
  # Check requirements
  check_requirements
  
  # Show banner
  echo "${BOLD}============================================${RESET}"
  echo "${BOLD}Ralphy${RESET} - Running until PRD is complete"
  local engine_display
  case "$AI_ENGINE" in
    opencode) engine_display="${CYAN}OpenCode${RESET}" ;;
    cursor) engine_display="${YELLOW}Cursor Agent${RESET}" ;;
    codex) engine_display="${BLUE}Codex${RESET}" ;;
    g3) engine_display="${GREEN}g3 (Local)${RESET}" ;;
    *) engine_display="${MAGENTA}Claude Code${RESET}" ;;
  esac
  echo "Engine: $engine_display"
  echo "Source: ${CYAN}$PRD_SOURCE${RESET} (${PRD_FILE:-$GITHUB_REPO})"
  echo "${BOLD}============================================${RESET}"
  
  # Sequential main loop
  while true; do
    ((iteration++))
    local result_code=0
    run_single_task "" "$iteration" || result_code=$?
    
    case $result_code in
      0)
        # Success, continue
        ;;
      1)
        # Error, but continue to next task
        log_warn "Task failed after $MAX_RETRIES attempts, continuing..."
        ;;
      2)
        # All tasks complete
        show_summary
        notify_done
        exit 0
        ;;
    esac
    
    # Check max iterations
    if [[ $MAX_ITERATIONS -gt 0 ]] && [[ $iteration -ge $MAX_ITERATIONS ]]; then
      log_warn "Reached max iterations ($MAX_ITERATIONS)"
      show_summary
      notify_done "Ralphy stopped after $MAX_ITERATIONS iterations"
      exit 0
    fi
    
    # Small delay between iterations
    sleep 1
  done
}

# Run main
main "$@"
