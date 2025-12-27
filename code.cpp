#include <iostream>
#include <vector>
#include <unordered_map>
#include <stack>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <sys/stat.h>
#include <cstdlib>
#include <map>
#include <cmath>
#include <limits>
#include <algorithm>
#include <cctype>

using namespace std;

class ExpenseSplitter {
private:
    struct Expense {
        string payer;
        double amount;
        string category;
        string note;
        string group;
        bool isRecurring;
        string timestamp;
        vector<pair<string, double>> shares; // {person, amount}

        Expense(string p, double a, string cat, string n, string g, bool r, string t, vector<pair<string, double>> s)
            : payer(p), amount(a), category(cat), note(n), group(g), isRecurring(r), timestamp(t), shares(s) {}
    };


    unordered_map<string, double> balances;
    vector<Expense> expenses;
    stack<pair<vector<Expense>, unordered_map<string, double>>> undoStack;
    stack<pair<vector<Expense>, unordered_map<string, double>>> redoStack;
    const string expenseFilename = "expenses.csv";
    const string balanceFilename = "balances.csv";

    string getCurrentTimestamp() {
        time_t now = time(0);
        char buffer[80];
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&now));
        return string(buffer);
    }


    string generateQRCode(const string& data) {
        if (system("which qrencode > /dev/null 2>&1") != 0) {
            return "QRencode tool not found!";
        }
        string command = "qrencode -o qr.png '" + data + "'";
        system(command.c_str());
        return "QR Code generated as qr.png";
    }


    bool fileExists(const string& filename) {
        struct stat buffer;
        return (stat(filename.c_str(), &buffer) == 0);
    }


    void pushToUndoStack() {
        undoStack.push({expenses, balances});
        while (!redoStack.empty()) redoStack.pop();
    }


    void printExpense(const Expense& e, int index = -1) {
        if (index != -1) {
            cout << "\n[" << index + 1 << "] ";
        }

        cout << e.payer << " paid " << fixed << setprecision(2) << e.amount << " INR"
             << " for " << e.category;

        if (!e.group.empty()) cout << " (Group: " << e.group << ")";
        if (!e.note.empty()) cout << " [Note: " << e.note << "]";
        if (e.isRecurring) cout << " [Recurring]";
        cout << endl;
        cout << "Timestamp 🕒: " << e.timestamp << "\nSplit among 👥: ";
        for (const auto& s : e.shares) {
            cout << s.first << "(" << s.second << " INR) ";
        }
        cout << "\n" << string(50, '-') << endl;
    }

public:
    ExpenseSplitter() {
        loadFromFile();
    }

    void addExpense(const string& payer, double amount, const vector<pair<string, double>>& participants, const string& category, const string& note = "", bool isRecurring = false, const string& group = "") {
        if (balances.find(payer) == balances.end()) {
            cout << endl;
            cout << "‼️ Payer not found.  Please add participant first." << endl;
            return;
        }

        // Verify all participants exist
        for (const auto& person : participants) {
            if (balances.find(person.first) == balances.end()) {
                cout << "‼️ Participant " << person.first << " not found. Please add them first." << endl;
                return;
            }
        }

        // Verify the sum of participant amounts matches the total expense
        double participantTotal = 0.0;
        for (const auto& person : participants) {
            participantTotal += person.second;
        }

        if (std::fabs(participantTotal - amount) > 0.01) {
            cout << "Error ⚠️: Sum of participant shares (" << participantTotal << ") does not equal the expense amount (" << amount << ")." << endl;
            return;
        }

        pushToUndoStack();

        // Update balances
        for (const auto& person : participants) {
            if (person.first != payer) {
                balances[person.first] -= person.second;
                balances[payer] += person.second;
            }
        }

        // Record the expense
        expenses.emplace_back(payer, amount, category, note, group, isRecurring, getCurrentTimestamp(), participants);
        saveToFile();
    }

    void viewExpenses() {
        cout << endl;
        cout << "\nAll Expenses:" << endl;
        if (expenses.empty()) {
            cout << "‼️ No expenses yet." << endl;
            return;
        }
        for (size_t i = 0; i < expenses.size(); ++i) {
            printExpense(expenses[i], i);
        }
    }

    void deleteExpense(size_t index) {
        if (index >= expenses.size()) {
            cout << "⚠️ Invalid expense index." << endl;
            return;
        }
        pushToUndoStack();

        // Adjust balances for the deleted expense
        const Expense& e = expenses[index];
        for (const auto& person : e.shares) {
            if (person.first != e.payer) {
                balances[person.first] += person.second;
                balances[e.payer] -= person.second;
            }
        }

        expenses.erase(expenses.begin() + index);
        cout << "Expense deleted and balances adjusted. ✅" << endl;
        saveToFile();
    }

    void viewBalances() {
        cout << "\nBalances (Who owes whom and how much for which category):" << endl;
    
        map<string, unordered_map<string, map<string, double>>> debtRelations;
    
        // Step 1: Build normal debts from non-settlement expenses
        for (const auto& expense : expenses) {
            if (expense.category == "Settlement") continue;
            for (const auto& share : expense.shares) {
                if (share.first != expense.payer) {
                    debtRelations[share.first][expense.payer][expense.category] += share.second;
                    debtRelations[expense.payer][share.first][expense.category] -= share.second;
                }
            }
        }
    
        // Step 2: Apply settlements
        for (const auto& expense : expenses) {
            if (expense.category == "Settlement") {
                string payer = expense.payer;
                string receiver = expense.shares[0].first; // settlement has only one participant
                double amount = expense.shares[0].second;
    
                // Settlements reduce total amount owed
                double remaining = amount;
    
                // Try reducing from existing debts between payer and receiver
                for (auto& category_debt_pair : debtRelations[payer][receiver]) {
                    auto& category = category_debt_pair.first;
                    auto& debt = category_debt_pair.second;
                
                    if (debt < -0.01) { // payer owes receiver
                        double repay = min(-debt, remaining);
                        debt += repay; // move towards zero
                        debtRelations[receiver][payer][category] -= repay;
                        remaining -= repay;
                        if (remaining < 0.01) break;
                    }
                }
            }
        }
    
        bool hasDebt = false;
    
        // Step 3: Display debts
        for (const auto& debtor : debtRelations) {
            bool printedDebtor = false;
    
            for (const auto& creditor : debtor.second) {
                for (const auto& categoryDebt : creditor.second) {
                    if (categoryDebt.second > 0.01) {
                        if (!printedDebtor) {
                            cout << debtor.first << ":" << endl;
                            printedDebtor = true;
                        }
                        cout << "  ₹" << fixed << setprecision(2) << categoryDebt.second
                             << " for " << categoryDebt.first << " ---> " << creditor.first << endl;
                        hasDebt = true;
                    }
                }
            }
        }
    
        if (!hasDebt) {
            cout << "All accounts are settled up! ✅🤝🏻" << endl;
        }
    }    

    void settleUp(const string& payer, const string& receiver, double amount) {
        if (balances.find(payer) == balances.end() || balances.find(receiver) == balances.end()) {
            cout << "‼️ Payer or receiver not found." << endl;
            return;
        }

        if (payer == receiver) {
            cout << "Error ⚠️: Payer and receiver cannot be the same person." << endl;
            return;
        }

        pushToUndoStack();
        balances[payer] -= amount;
        balances[receiver] += amount;

        vector<pair<string, double>> participants = {{receiver, amount}};
        expenses.emplace_back(payer, amount, "Settlement", "", "", false, getCurrentTimestamp(), participants);

        string message = payer + " paid ₹" + to_string(amount) + " to " + receiver + " on " + getCurrentTimestamp();
        cout << message << endl;
        cout << generateQRCode(message) << endl;
        saveToFile();
    }

    void undo() {
        if (undoStack.empty()) {
            cout << "‼️ Nothing to undo." << endl;
            return;
        }
        redoStack.push({expenses, balances});
        auto state = undoStack.top();
        undoStack.pop();
        expenses = state.first;
        balances = state.second;
        saveToFile();
        cout << "Undo successful. ✅" << endl;
    }

    void redo() {
        if (redoStack.empty()) {
            cout << "‼️ Nothing to redo." << endl;
            return;
        }
        undoStack.push({expenses, balances});
        auto state = redoStack.top();
        redoStack.pop();
        expenses = state.first;
        balances = state.second;
        saveToFile();
        cout << "Redo successful. ✅" << endl;
    }

    void viewParticipants() {
        cout << endl;
        cout << "\nParticipants 👥:" << endl;
        if (balances.empty()) {
            cout << "‼️ No participants added." << endl;
            return;
        }
        for (const auto& entry : balances) {
            cout << "- " << entry.first << endl;
        }
    }

    void addParticipant(const string& name) {
        if (name.empty()) {
            cout << "Error ⚠️: Participant name cannot be empty." << endl;
            return;
        }

        if (balances.find(name) != balances.end()) {
            cout << "‼️ Participant already exists." << endl;
            return;
        }
        pushToUndoStack();
        balances[name] = 0.0;
        saveToFile();
        cout << "Participant added. ✅" << endl;
    }

    void showCategorySummary() {
        // A map to store category and a list of individual expenses under that category
        map<string, vector<pair<pair<double, vector<string>>, string>>> categoryDetails;
    
        // Loop through each expense and add its amount, participants, and timestamp under the respective category
        for (const auto& e : expenses) {
            if (e.category != "Settlement") {
                vector<string> participants;
                for (const auto& s : e.shares) {
                    participants.push_back(s.first);
                }
                categoryDetails[e.category].push_back({{e.amount, participants}, e.timestamp});
            }
        }
    
        cout << "\nCategory Summary (in INR):" << endl;
        if (categoryDetails.empty()) {
            cout << "‼️ No expenses in any categories yet." << endl;
            return;
        }
    
        // Print out each category and its details
        for (const auto& entry : categoryDetails) {
            cout << entry.first << ":\n";
    
            for (size_t i = 0; i < entry.second.size(); ++i) {
                // Print individual expense in the category with timestamp as identifier
                cout << "  " << entry.second[i].second << ": ₹" << fixed << setprecision(2) << entry.second[i].first.first << endl;
                cout << "  Participants 👥: ";
                for (const auto& participant : entry.second[i].first.second) {
                    cout << participant << " ";
                }
                cout << "\n" << string(50, '-') << endl;
            }
        }
    }    

    void saveToFile() {
        try {
            // Open the expense file for writing
            ofstream expenseFile(expenseFilename);
            if (!expenseFile.is_open()) {
                throw ios_base::failure("Could not open the expense file for writing.");
            }
    
            // Save expenses
            expenseFile << "Payer,Amount,Category,Note,Group,IsRecurring,Timestamp,Participants" << endl;
            for (const auto& e : expenses) {
                expenseFile << e.payer << "," << e.amount << "," << e.category << ","
                            << e.note << "," << e.group << ","
                            << e.isRecurring << "," << e.timestamp;
                for (const auto& s : e.shares) {
                    expenseFile << "," << s.first << ":" << s.second;
                }
                expenseFile << endl;
            }
            expenseFile.close();  // Explicitly close the file
    
            // Open the balance file for writing
            ofstream balanceFile(balanceFilename);
            if (!balanceFile.is_open()) {
                throw ios_base::failure("Could not open the balance file for writing.");
            }
    
            // Save balances
            balanceFile << "Participant,Balance(INR)" << endl;
            for (const auto& entry : balances) {
                balanceFile << entry.first << "," << fixed << setprecision(2) << entry.second << endl;
            }
            balanceFile.close();  // Explicitly close the file
    
        } catch (const ios_base::failure& e) {
            cerr << "File error: " << e.what() << endl;
        } catch (const exception& e) {
            cerr << "An error occurred: " << e.what() << endl;
        }
    }

    void loadFromFile() {
        // Load balances
        if (fileExists(balanceFilename)) {
            ifstream balanceFile(balanceFilename);
            if (balanceFile.is_open()) {
                string line;
                getline(balanceFile, line); // Skip header
                while (getline(balanceFile, line)) {
                    if (!line.empty()) {
                        size_t comma = line.find(',');
                        if (comma != string::npos) {
                            string name = line.substr(0, comma);
                            double balance = stod(line.substr(comma + 1));
                            balances[name] = balance;
                        }
                    }
                }
                balanceFile.close();
            }
        }

        // Load expenses
        if (fileExists(expenseFilename)) {
            ifstream expenseFile(expenseFilename);
            if (expenseFile.is_open()) {
                string line;
                getline(expenseFile, line); // Skip header
                while (getline(expenseFile, line)) {
                    if (!line.empty()) {
                        vector<string> parts;
                        stringstream ss(line);
                        string item;
                        while (getline(ss, item, ',')) {
                            parts.push_back(item);
                        }

                        if (parts.size() >= 7) {
                            string payer = parts[0];
                            double amount = stod(parts[1]);
                            string category = parts[2];
                            string note = parts[3];
                            string group = parts[4];
                            bool isRecurring = parts[5] == "1" ? true : false;
                            string timestamp = parts[6];
                            vector<pair<string, double>> shares;
                            for (size_t i = 7; i < parts.size(); ++i) {
                                size_t colonPos = parts[i].find(':');
                                if (colonPos != string::npos) {
                                    string participant = parts[i].substr(0, colonPos);
                                    double shareAmount = stod(parts[i].substr(colonPos + 1));
                                    shares.push_back({participant, shareAmount});
                                }
                            }

                            expenses.push_back(Expense(payer, amount, category, note, group, isRecurring, timestamp, shares));
                        }
                    }
                }
                expenseFile.close();
            }
        }
    }
};

int main() {
    ExpenseSplitter splitter;
    bool running = true;
    int choice;

    while (running) {
        cout << "\nExpense Splitter Menu:" << endl;
        cout << "1. Add Participant" << endl;
        cout << "2. View Participants" << endl;
        cout << "3. Add Expense" << endl;
        cout << "4. View Expenses" << endl;
        cout << "5. Delete Expense" << endl;
        cout << "6. View Balances" << endl;
        cout << "7. Settle Up" << endl;
        cout << "8. Undo" << endl;
        cout << "9. Redo" << endl;
        cout << "10. View Category Summary" << endl;
        cout << "0. Exit" << endl;
        cout << "Enter choice: ";
        cin >> choice;

        switch (choice) {
            case 1: {
                string name;
                cout << "Enter participant name 👤: ";
                cin >> name;
                splitter.addParticipant(name);
                break;
            }
            case 2:
                splitter.viewParticipants();
                break;
            case 3: {
                string payer, category, note, group;
                double amount;
                int numParticipants;

                cout << "Enter payer's name: ";
                cin >> payer;
                cout << "Enter expense amount (INR): ";
                cin >> amount;
                cout << "Enter expense category: ";
                cin >> category;
                cout << "Enter note (optional): ";
                cin.ignore();
                getline(cin, note);
                cout << "Enter group name (optional): ";
                getline(cin, group);
                cout << "Is this a recurring expense? (1 for Yes, 0 for No): ";
                bool isRecurring;
                cin >> isRecurring;

                cout << "Enter number of participants (including payer) 👥: ";
                cin >> numParticipants;

                vector<pair<string, double>> participants;
                for (int i = 0; i < numParticipants; ++i) {
                    string participant;
                    double share;
                    cout << "Enter participant 👤 " << i + 1 << " name: ";
                    cin >> participant;
                    cout << "Enter share amount for " << participant << ": ";
                    cin >> share;
                    participants.push_back({participant, share});
                }

                splitter.addExpense(payer, amount, participants, category, note, isRecurring, group);
                break;
            }
            case 4:
                splitter.viewExpenses();
                break;
            case 5: {
                size_t index;
                cout << "Enter expense index to delete: ";
                cin >> index;
                splitter.deleteExpense(index - 1);  // 1-based index
                break;
            }
            case 6:
                splitter.viewBalances();
                break;
            case 7: {
                string payer, receiver;
                double amount;
                cout << "Enter payer's name: ";
                cin >> payer;
                cout << "Enter receiver's name: ";
                cin >> receiver;
                cout << "Enter amount to settle (INR): ";
                cin >> amount;
                splitter.settleUp(payer, receiver, amount);
                break;
            }
            case 8:
                splitter.undo();
                break;
            case 9:
                splitter.redo();
                break;
            case 10:
                splitter.showCategorySummary();
                break;
            case 0:
                running = false;
                break;
            default:
                cout << "‼️ Invalid choice!" << endl;
        }
    }

    return 0;
}
