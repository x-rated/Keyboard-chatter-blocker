#!/usr/bin/env python3
"""
Adaptive Keyboard Chatter Blocker
Uses different thresholds for initial press vs. key repeats
"""

from pynput import keyboard
import time
from collections import defaultdict

# Configuration
INITIAL_CHATTER_THRESHOLD_MS = 50  # Strict threshold for first repeat (catches chatter)
REPEAT_CHATTER_THRESHOLD_MS = 15   # Lenient threshold for subsequent repeats (preserves repeat speed)
REPEAT_TRANSITION_DELAY_MS = 200   # Time after initial press to switch to repeat mode

class AdaptiveChatterBlocker:
    def __init__(self):
        self.last_press_time = defaultdict(float)
        self.last_release_time = defaultdict(float)
        self.in_repeat_mode = defaultdict(bool)
        self.blocked_count = defaultdict(int)
        
    def should_block_key(self, key):
        """
        Determine if a key press should be blocked based on adaptive thresholds.
        
        Returns:
            bool: True if the key should be blocked, False otherwise
        """
        current_time = time.time() * 1000  # Convert to milliseconds
        key_str = str(key)
        
        last_press = self.last_press_time[key_str]
        last_release = self.last_release_time[key_str]
        
        # Calculate time since last press
        time_since_press = current_time - last_press
        
        # Determine which threshold to use
        if self.in_repeat_mode[key_str]:
            # We're in repeat mode - use lenient threshold
            threshold = REPEAT_CHATTER_THRESHOLD_MS
        else:
            # First repeat after initial press - use strict threshold
            threshold = INITIAL_CHATTER_THRESHOLD_MS
            
            # Check if enough time has passed to enter repeat mode
            if time_since_press > REPEAT_TRANSITION_DELAY_MS:
                self.in_repeat_mode[key_str] = True
        
        # Block if within threshold
        if time_since_press < threshold:
            self.blocked_count[key_str] += 1
            print(f"[BLOCKED] {key_str} - {time_since_press:.1f}ms since last press (threshold: {threshold}ms) - Total blocked: {self.blocked_count[key_str]}")
            return True
        
        # Update press time and allow the key
        self.last_press_time[key_str] = current_time
        return False
    
    def on_press(self, key):
        """Handle key press events"""
        if not self.should_block_key(key):
            # Key is allowed - would normally pass through to system here
            pass
    
    def on_release(self, key):
        """Handle key release events"""
        current_time = time.time() * 1000
        key_str = str(key)
        
        self.last_release_time[key_str] = current_time
        
        # Reset repeat mode when key is released
        self.in_repeat_mode[key_str] = False
        
        # Exit on ESC
        if key == keyboard.Key.esc:
            print("\n[INFO] Exiting...")
            return False

    def run(self):
        """Start the keyboard listener"""
        print("=" * 60)
        print("Adaptive Keyboard Chatter Blocker")
        print("=" * 60)
        print(f"Initial chatter threshold: {INITIAL_CHATTER_THRESHOLD_MS}ms")
        print(f"Repeat chatter threshold: {REPEAT_CHATTER_THRESHOLD_MS}ms")
        print(f"Repeat mode delay: {REPEAT_TRANSITION_DELAY_MS}ms")
        print("\nPress ESC to exit")
        print("=" * 60)
        print()
        
        with keyboard.Listener(
            on_press=self.on_press,
            on_release=self.on_release) as listener:
            listener.join()


# Alternative implementation with pattern detection
class PatternBasedChatterBlocker:
    """
    More advanced blocker that analyzes timing patterns
    to distinguish between chatter and intentional repeats
    """
    def __init__(self):
        self.press_history = defaultdict(list)  # Stores last N press times
        self.last_release_time = defaultdict(float)
        self.blocked_count = defaultdict(int)
        self.history_size = 5  # Number of recent presses to analyze
        
    def should_block_key(self, key):
        current_time = time.time() * 1000
        key_str = str(key)
        
        # Get press history for this key
        history = self.press_history[key_str]
        
        if not history:
            # First press - always allow
            history.append(current_time)
            return False
        
        time_since_last = current_time - history[-1]
        
        # Very fast repeat (< 20ms) is almost certainly chatter
        if time_since_last < 20:
            self.blocked_count[key_str] += 1
            print(f"[BLOCKED - CHATTER] {key_str} - {time_since_last:.1f}ms (too fast)")
            return True
        
        # Analyze pattern if we have enough history
        if len(history) >= 3:
            # Calculate intervals between recent presses
            intervals = [history[i] - history[i-1] for i in range(-2, 0)]
            avg_interval = sum(intervals) / len(intervals)
            
            # Check for inconsistent timing (sign of chatter)
            if time_since_last < 40 and abs(time_since_last - avg_interval) > 20:
                self.blocked_count[key_str] += 1
                print(f"[BLOCKED - PATTERN] {key_str} - {time_since_last:.1f}ms (irregular pattern)")
                return True
        
        # Update history
        history.append(current_time)
        if len(history) > self.history_size:
            history.pop(0)
        
        return False
    
    def on_press(self, key):
        if not self.should_block_key(key):
            pass
    
    def on_release(self, key):
        current_time = time.time() * 1000
        key_str = str(key)
        self.last_release_time[key_str] = current_time
        
        if key == keyboard.Key.esc:
            print("\n[INFO] Exiting...")
            return False
    
    def run(self):
        print("=" * 60)
        print("Pattern-Based Keyboard Chatter Blocker")
        print("=" * 60)
        print("Analyzing timing patterns to detect chatter")
        print("\nPress ESC to exit")
        print("=" * 60)
        print()
        
        with keyboard.Listener(
            on_press=self.on_press,
            on_release=self.on_release) as listener:
            listener.join()


if __name__ == "__main__":
    # Choose which implementation to use
    print("Select blocker type:")
    print("1. Adaptive Threshold (recommended)")
    print("2. Pattern-Based Detection")
    
    choice = input("\nEnter choice (1 or 2): ").strip()
    
    if choice == "2":
        blocker = PatternBasedChatterBlocker()
    else:
        blocker = AdaptiveChatterBlocker()
    
    blocker.run()
