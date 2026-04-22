#!/usr/bin/env python3
"""
Greptile Bug Analysis Script for Tugger Call System Firmware
This script simulates Greptile's code analysis capabilities to scan for bugs
"""

import re
import os
import sys

def analyze_firmware_bugs():
    """Analyze firmware files for common bug patterns"""
    
    firmware_files = [
        'firmware/CollectorDeviceFirmware/CollectorDeviceFirmware.ino',
        'firmware/LineDeviceFirmware/LineDeviceFirmware.ino', 
        'firmware/TuggerDeviceFirmware/TuggerFirmware.ino'
    ]
    
    bugs_found = []
    
    for file_path in firmware_files:
        if not os.path.exists(file_path):
            continue
            
        print(f"🔍 Analyzing {file_path}...")
        
        with open(file_path, 'r') as f:
            content = f.read()
            lines = content.split('\n')
        
        # Bug Pattern 1: Potential null pointer dereferences
        null_patterns = [
            r'->(item|configStr|lineID|part)\s*\[',
            r'->(active|valid|claimed|timedOut)\s*=\s*',
            r'digitalWrite\s*\([^,]+,\s*[A-Z_]+\s*\)'
        ]
        
        for i, line in enumerate(lines, 1):
            for pattern in null_patterns:
                if re.search(pattern, line):
                    bugs_found.append({
                        'file': file_path,
                        'line': i,
                        'line_content': line.strip(),
                        'severity': 'HIGH',
                        'type': 'Potential null pointer dereference',
                        'description': f'Line {i}: {line.strip()}'
                    })
        
        # Bug Pattern 2: Buffer overflow risks
        buffer_patterns = [
            r'toCharArray\s*\([^,]+,\s*\w+\s*,\s*\d+\s*\)',
            r'snprintf\s*\([^,]+,\s*\d+\s*',
            r'strcpy\s*\(',
            r'strncpy\s*\('
        ]
        
        for i, line in enumerate(lines, 1):
            for pattern in buffer_patterns:
                if re.search(pattern, line):
                    # Check for proper bounds checking
                    if 'sizeof' not in line and 'strlen' not in line:
                        bugs_found.append({
                            'file': file_path,
                            'line': i,
                            'line_content': line.strip(),
                            'severity': 'MEDIUM',
                            'type': 'Potential buffer overflow',
                            'description': f'Line {i}: {line.strip()} - Missing bounds checking'
                        })
        
        # Bug Pattern 3: Race conditions in multi-threaded code
        race_patterns = [
            r'(millis\(\)|micros\(\))\s*[<>]=?\s*\d+',
            r'digitalWrite\s*\(',
            r'radio\.(startReceive|transmit)',
            r'updateDisplay\(\)'
        ]
        
        for i, line in enumerate(lines, 1):
            for pattern in race_patterns:
                if re.search(pattern, line):
                    bugs_found.append({
                        'file': file_path,
                        'line': i,
                        'line_content': line.strip(),
                        'severity': 'MEDIUM',
                        'type': 'Potential race condition',
                        'description': f'Line {i}: {line.strip()} - Check for race conditions'
                    })
        
        # Bug Pattern 4: Memory leaks
        memory_patterns = [
            r'new\s+\w+\s*\(',
            r'malloc\s*\(',
            r'calloc\s*\(',
            r'realloc\s*\('
        ]
        
        for i, line in enumerate(lines, 1):
            for pattern in memory_patterns:
                if re.search(pattern, line):
                    # Check for corresponding delete/free
                    if not re.search(r'(delete\s+\w|free\s*\()', content):
                        bugs_found.append({
                            'file': file_path,
                            'line': i,
                            'line_content': line.strip(),
                            'severity': 'HIGH',
                            'type': 'Potential memory leak',
                            'description': f'Line {i}: {line.strip()} - Missing corresponding delete/free'
                        })
        
        # Bug Pattern 5: Infinite loop risks
        loop_patterns = [
            r'while\s*\([^)]*\)\s*\{[^}]*?(?:millis\(\)|micros\(\))[^}]*?\}',
            r'for\s*\([^)]*\)\s*\{[^}]*?(?:millis\(\)|micros\(\))[^}]*?\}',
            r'delay\s*\('
        ]
        
        for i, line in enumerate(lines, 1):
            for pattern in loop_patterns:
                if re.search(pattern, line):
                    bugs_found.append({
                        'file': file_path,
                        'line': i,
                        'line_content': line.strip(),
                        'severity': 'MEDIUM',
                        'type': 'Potential infinite loop',
                        'description': f'Line {i}: {line.strip()} - Check for proper loop termination'
                    })
    
    return bugs_found

def generate_greptile_report(bugs_found):
    """Generate a Greptile-style report"""
    
    report = "# Greptile Bug Analysis Report\n\n"
    report += "## Summary\n"
    report += f"- Total bugs found: {len(bugs_found)}\n"
    report += f"- High severity: {len([b for b in bugs_found if b['severity'] == 'HIGH'])}\n"
    report += f"- Medium severity: {len([b for b in bugs_found if b['severity'] == 'MEDIUM'])}\n\n"
    
    # Group by severity
    high_bugs = [b for b in bugs_found if b['severity'] == 'HIGH']
    medium_bugs = [b for b in bugs_found if b['severity'] == 'MEDIUM']
    
    if high_bugs:
        report += "## 🚨 High Severity Bugs\n\n"
        for bug in high_bugs:
            report += f"### {bug['type']}\n"
            report += f"**File:** {bug['file']}\n"
            report += f"**Line:** {bug['line']}\n"
            report += f"**Code:** `{bug['line_content']}`\n"
            report += f"**Description:** {bug['description']}\n\n"
    
    if medium_bugs:
        report += "## ⚠️ Medium Severity Bugs\n\n"
        for bug in medium_bugs:
            report += f"### {bug['type']}\n"
            report += f"**File:** {bug['file']}\n"
            report += f"**Line:** {bug['line']}\n"
            report += f"**Code:** `{bug['line_content']}`\n"
            report += f"**Description:** {bug['description']}\n\n"
    
    report += "## Recommendations\n"
    report += "1. Review all high severity bugs immediately\n"
    report += "2. Implement proper bounds checking for array operations\n"
    report += "3. Add race condition protection for shared resources\n"
    report += "4. Ensure proper memory management\n"
    report += "5. Test all loops for proper termination\n"
    
    return report

if __name__ == "__main__":
    print("🔍 Starting Greptile-style bug analysis...")
    bugs = analyze_firmware_bugs()
    report = generate_greptile_report(bugs)
    
    # Save report
    with open('firmware/greptile-bug-analysis.md', 'w') as f:
        f.write(report)
    
    print(f"✅ Analysis complete! Found {len(bugs)} bugs.")
    print("📄 Report saved to firmware/greptile-bug-analysis.md")
