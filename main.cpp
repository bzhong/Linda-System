#include <iostream>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <iterator>
#include <unordered_map>
#include <vector>
#include <string>
#include <cstdlib>
#include <pthread.h>

using namespace std;

string funcConstructor1 = "#include <cstdlib>\n";
string funcConstructor2 = "int main(int argc, char** argv){return @(atoi(argv[1]));}";
struct ThreadArgs {
    int threadNum;
    string filePath;
};

typedef unordered_map<int, vector<vector<pair<int, string> > > > Tuples;
typedef vector<vector<pair<int, string> > > Results;
typedef vector<pair<int, string> > Values;
typedef vector<unordered_map<string, string> > LocalVars;

LocalVars funcSet;
LocalVars localVars;
Tuples tupleSpace;
pthread_mutex_t mutex;
pthread_mutex_t execFuncMutex;
pthread_mutex_t test_m;

pair<int, string> ParseEvalElem(string cmd, int threadNum) {
    int varNameEndPos = cmd.find('(');
    int paramEndPos = cmd.find(')');
    string varName = cmd.substr(0, varNameEndPos);
    string param = cmd.substr(varNameEndPos + 1, paramEndPos - varNameEndPos - 1);
    if (funcSet[threadNum].find(varName) == funcSet[threadNum].end()) {
        cerr << "can't find function " + varName << ". failed and exit."<< endl;
        exit(-1);
    }
    string programCmd = "./" + funcSet[threadNum].at(varName) + " " + param;
    pthread_mutex_lock(&execFuncMutex);
    int status = system(programCmd.c_str());
    pthread_mutex_unlock(&execFuncMutex);
    string realValue = to_string((long long int)WEXITSTATUS(status));
    if (realValue.find('.') != string::npos) {
        return pair<int, string>(5, realValue);
    }
    else {
        return pair<int, string>(1, realValue);
    }
}

/*** explanation of encoding rule ***/
/* 1 if integer e.g. 3 */
/* 2 if integer varible e.g. i1 for out */
/* 3 if integer varible e.g. ?i1 for in */
/* 4 if integer varible e.g. ?i for in */
/* 5 if double e.g. 3.0 */
/* 6 if double varible e.g. d1 for out */
/* 7 if double varible e.g. ?d1 for in */
/* 8 if double varible e.g. ?d for in */
/* 9 if string e.g. "abc" */
/* 10 if string varible e.g. s1 for out */
/* 11 if string varible e.g. ?s1 for in */
/* 12 if string varible e.g. ?s for in */
pair<int, string> ParseElements(string elem, int threadNum) {
    int startPos = elem.find_first_not_of(' ');
    int end = elem.find_last_not_of(' ');
    string line = elem.substr(startPos, end - startPos + 1);
    startPos = 0;
    if (line[startPos] == '\"') {
        string tmpData;
        ++startPos;
        int stringLen = 0;
        while (line[startPos + stringLen] != '\"') {
            ++stringLen;
        }
        tmpData = line.substr(startPos, stringLen);
        return pair<int, string>(9, tmpData);
    }
    else if (line[startPos] == '?') {
        string tmpData;
        tmpData = line.substr(startPos + 1);
        int stringLen = tmpData.size();
        if (stringLen == 1) {
            switch(tmpData[0]) {
                case 'i':
                    return pair<int, string>(4, tmpData);
                    break;
                case 'd':
                    return pair<int, string>(8, tmpData);
                    break;
                case 's':
                    return pair<int, string>(12, tmpData);
                    break;
            }
        }
        else {
            switch(tmpData[0]) {
                case 'i':
                    return pair<int, string>(3, tmpData);
                    break;
                case 'd':
                    return pair<int, string>(7, tmpData);
                    break;
                case 's':
                    return pair<int, string>(11, tmpData);
                    break;
            }
        }
    }
    else {
        string tmpData;
        tmpData = line.substr(startPos);
        if (tmpData.find('(') != string::npos) {
            return ParseEvalElem(tmpData, threadNum);
        }
        switch (tmpData[0]) {
            case 'i':
                return pair<int, string>(2, tmpData);
                break;
            case 'd':
                return pair<int, string>(6, tmpData);
                break;
            case 's':
                return pair<int, string>(10, tmpData);
                break;
            default:
                if (tmpData.find('.') != string::npos) {
                    return pair<int, string>(5, tmpData);
                }
                else
                {
                    return pair<int, string>(1, tmpData);
                }
                break;
        }
    }
    return pair<int, string>(-1, "");
}

Values GetValues(string line, int startPos, int& numberOfElem, int threadNum) {
    string tmp;
    vector<string> elements;
    Values values;
    int begin, end;
    begin = line.find_first_of('(');
    end = line.find_last_of(')');
    tmp = line.substr(begin + 1, end - begin - 1);
    istringstream iss(tmp);
    string token;
    while (getline(iss, token, ',')) {
        elements.push_back(token);
    }
    numberOfElem = elements.size();
    for (unsigned int i = 0; i < elements.size(); ++i) {
        values.push_back(ParseElements(elements[i], threadNum));
    }
    return values;
}

void PrintValues(Values values) {
    pthread_mutex_lock(&test_m);
    string res, tmp;
    for (unsigned int i = 0; i < values.size(); ++i) {
        tmp = to_string((long long int)values[i].first) + ":" + values[i].second + "\n";
        res += tmp;
    }
    cout << "Print Values: " << res << endl;
    pthread_mutex_unlock(&test_m);
}

void ParseOutCmd(string line, Tuples& tupleSpace, int threadNum) {
    int startPos = 0, numberOfElem = 0;
    Values values = GetValues(line, startPos, numberOfElem, threadNum);
    pthread_mutex_lock(&mutex);
    for (unsigned int i = 0; i < values.size(); ++i) {
        if ((values[i].first == 2 || values[i].first == 6 || values[i].first == 10) &&
            localVars[threadNum].find(values[i].second) != localVars[threadNum].end()) {
            values[i].second = localVars[threadNum][values[i].second];
            values[i].first -= 1; //change status of values[i] to non-variable
        }
        else if (values[i].first == 1 || values[i].first == 5 || values[i].first == 9) {
            continue;
        }
        else {
            cerr << "out command illegal. failed for invalid variables" << endl;
            pthread_mutex_unlock(&mutex);
            exit(-1);
        }
    }
    tupleSpace[numberOfElem].push_back(values);
    pthread_mutex_unlock(&mutex);
}

void ParseEvalCmd(string line, Tuples& tupleSpace, int threadNum) {
    int startPos = 0, numberOfElem = 0;
    Values values = GetValues(line, startPos, numberOfElem, threadNum);
    pthread_mutex_lock(&mutex);
    for (unsigned int i = 0; i < values.size(); ++i) {
        if ((values[i].first == 2 || values[i].first == 6 || values[i].first == 10) &&
            localVars[threadNum].find(values[i].second) != localVars[threadNum].end()) {
            values[i].second = localVars[threadNum][values[i].second];
            values[i].first -= 1; //change status of values[i] to non-variable
        }
        else if (values[i].first == 1 || values[i].first == 5 || values[i].first == 9) {
            continue;
        }
        else {
            cerr << "eval command illegal. failed for invalid variables" << endl;
            pthread_mutex_unlock(&mutex);
            exit(-1);
        }
    }
    tupleSpace[numberOfElem].push_back(values);
    pthread_mutex_unlock(&mutex);
}

string MergeValues(Values values) {
    string completeTuple;
    completeTuple += "(";
    for (unsigned int i = 0; i < values.size(); ++i) {
        if (values[i].first == 9) {
            completeTuple += "\"";
            completeTuple += values[i].second;
            completeTuple += "\"";
        }
        else {
            completeTuple += values[i].second;
        }
        if (i != values.size() - 1) {
            completeTuple += ", ";
        }
    }
    completeTuple += ")";
    return completeTuple;
}

bool IsMatchedValues(Values values, Values inData, int numberOfElem, int threadNum) {
    unordered_map<string, string> tmpLocalVars; // var name : value
    for (int i = 0; i < numberOfElem; ++i) {
        if (values[i].first == inData[i].first &&
                (values[i].first == 1 ||
                 values[i].first == 5 ||
                 values[i].first == 9)) {
            if (values[i].second != inData[i].second)
                return false;
        }
        else if ((values[i].first == 4 && inData[i].first == 1) || 
                 (values[i].first == 8 && inData[i].first == 5) ||
                 (values[i].first == 12 && inData[i].first == 9)) {
            continue;
        }
        else if (values[i].first == 3 && inData[i].first == 1) {
            tmpLocalVars[values[i].second] = inData[i].second;
        }
        else if (values[i].first == 7 && inData[i].first == 5) {
            tmpLocalVars[values[i].second] = inData[i].second;
        }
        else if (values[i].first == 11 && inData[i].first == 9) {
            tmpLocalVars[values[i].second] = inData[i].second;
        }
        else {
            return false;
        }
    }
    for (auto iter = tmpLocalVars.begin(); iter != tmpLocalVars.end(); ++iter) {
        localVars[threadNum][iter->first] = iter->second;
    }
    return true;
}

void ParseInCmd(string line, Tuples& tupleSpace, int threadNum) {
    int startPos = 0, numberOfElem = 0;
    Values values = GetValues(line, startPos, numberOfElem, threadNum);
    while (true) {
        pthread_mutex_lock(&mutex);
        if (tupleSpace.find(numberOfElem) != tupleSpace.end()) {
            Results &results = tupleSpace.at(numberOfElem);
            for (unsigned int i = 0; i < results.size(); ++i) {
                if (!IsMatchedValues(values, results[i], numberOfElem, threadNum)) {
                    continue;
                }
                else {
                    tupleSpace[numberOfElem].erase(tupleSpace[numberOfElem].begin() + i);
                    pthread_mutex_unlock(&mutex);
                    return;
                }
            }
        }
        pthread_mutex_unlock(&mutex);
    }
}

void ParseInpCmd(string line, Tuples& tupleSpace, int threadNum, bool& status) {
    int startPos = 0, numberOfElem = 0;
    Values values = GetValues(line, startPos, numberOfElem, threadNum);
    pthread_mutex_lock(&mutex);
    if (tupleSpace.find(numberOfElem) != tupleSpace.end()) {
        Results &results = tupleSpace.at(numberOfElem);
        for (unsigned int i = 0; i < results.size(); ++i) {
            if (!IsMatchedValues(values, results[i], numberOfElem, threadNum)) {
                continue;
            }
            else {
                tupleSpace[numberOfElem].erase(tupleSpace[numberOfElem].begin() + i);
                status = true;
                pthread_mutex_unlock(&mutex);
                return;
            }
        }
        status = false;
    }
    else {
        status = false;
    }
    pthread_mutex_unlock(&mutex);
}

void ParseRdCmd(string line, Tuples& tupleSpace, int threadNum) {
    int startPos = 0, numberOfElem = 0;
    Values values = GetValues(line, startPos, numberOfElem, threadNum);
    while (true) {
        pthread_mutex_lock(&mutex);
        if (tupleSpace.find(numberOfElem) != tupleSpace.end()) {
            Results &results = tupleSpace.at(numberOfElem);
            for (unsigned int i = 0; i < results.size(); ++i) {
               if (!IsMatchedValues(values, results[i], numberOfElem, threadNum)) {
                   continue;
               }
               else {
                   pthread_mutex_unlock(&mutex);
                   return;
               }
            }
        }
        pthread_mutex_unlock(&mutex);
    }
}

void ParseRdpCmd(string line, Tuples& tupleSpace, int threadNum, bool& status) {
    int startPos = 0, numberOfElem = 0;
    Values values = GetValues(line, startPos, numberOfElem, threadNum);
    pthread_mutex_lock(&mutex);
    if (tupleSpace.find(numberOfElem) != tupleSpace.end()) {
        Results &results = tupleSpace.at(numberOfElem);
        for (unsigned int i = 0; i < results.size(); ++i) {
           if (!IsMatchedValues(values, results[i], numberOfElem, threadNum)) {
               continue;
           }
           else {
               status = true;
               pthread_mutex_unlock(&mutex);
               return;
           }
        }
        status = false;
    }
    else {
        status = false;
    }
    pthread_mutex_unlock(&mutex);
}

void ParseDumpCmd(string line, Tuples& tupleSpace) {
    pthread_mutex_lock(&mutex);
    for (auto iter = tupleSpace.begin(); iter != tupleSpace.end(); ++iter) {
        Results res = iter->second;
        for (unsigned int i = 0; i < res.size(); ++i) {
            cout << MergeValues(res[i]) << endl;
        }
    }
    pthread_mutex_unlock(&mutex);
}

void ParseLine(string line, Tuples& tupleSpace, int threadNum) {
    int startPos = 0;
    bool status = false;
    while ((unsigned int)startPos < line.size() && isspace(line[startPos])) {
        ++startPos;
    }
    if ((unsigned int)startPos == line.size()) {
        return;
    }
    string commandPrefix = line.substr(startPos, 2);
    if (commandPrefix == "ou") {
        ParseOutCmd(line, tupleSpace, threadNum);
    }
    else if (commandPrefix == "in") {
        if (line[startPos + 2] == 'p') {
            ParseInpCmd(line, tupleSpace, threadNum, status);
        }
        else {
            ParseInCmd(line, tupleSpace, threadNum);
        }
    }
    else if (commandPrefix == "rd") {
        if (line[startPos + 2] == 'p') {
            ParseRdpCmd(line, tupleSpace, threadNum, status);
        }
        else {
        ParseRdCmd(line, tupleSpace, threadNum);
        }
    }
    else if (commandPrefix == "du") {
        ParseDumpCmd(line, tupleSpace);
    }
    else if (commandPrefix == "ev") {
        ParseEvalCmd(line, tupleSpace, threadNum);
    }
}

bool isConditionTrue2(string cond, int threadNum) {
    bool status = false;
    int pos = 0;
    string command;
    while (isspace(cond[pos])) {
        ++pos;
    }
    command = cond.substr(pos);
    if (command.substr(0, 3) == "inp") {
        ParseInpCmd(command, tupleSpace, threadNum, status);
        return status;
    }
    else if (command.substr(0, 3) == "rdp") {
        ParseRdpCmd(command, tupleSpace, threadNum, status);
        return status;
    }
    else {
        cerr << "invalid condition of If. no command allowed. failed and exit.";
        exit(-1);
    }
}

bool isConditionTrue(ifstream& cmdFile, int threadNum) {
    string command;
    int paren = 0;
    while (cmdFile.good()) {
        char ch = cmdFile.get();
        if (isspace(ch)) {
            continue;
        }
        else if (ch == '(') {
            ++paren;
            break;
        }
        else {
            cerr << "invalid condition of If. failed and exit.";
            exit(-1);
        }
    }
    while (cmdFile.good()) {
        char ch = cmdFile.get();
        if (ch == '(') {
            ++paren;
        }
        else if (ch == ')') {
            --paren;
            if (paren == 0) {
                break;
            }
        }
        command.append(1, ch);
    }
    if (command.size() < 3) {
        cerr << "invalid condition of If. failed and exit.";
        exit(-1);
    }
    bool status = false;
    if (command.substr(0, 3) == "inp") {
        ParseInpCmd(command, tupleSpace, threadNum, status);
        return status;
    }
    else if (command.substr(0, 3) == "rdp") {
        ParseRdpCmd(command, tupleSpace, threadNum, status);
        return status;
    }
    else {
        cerr << "invalid condition of If. no command allowed. failed and exit.";
        exit(-1);
    }
}

void ParseForLoop(ifstream& cmdFile, string& varName, int& startVal, int& endVal) {
    char curChar;
    string value;
    bool plusOne = false;
    while (cmdFile.good() && (curChar = cmdFile.get()) != '(')
        ;
    while (cmdFile.good() && (curChar = cmdFile.get()) != '=') {
        if (!isspace(curChar)) {
            varName.append(1, curChar);
        }
    }
    while (cmdFile.good() && (curChar = cmdFile.get()) != ';') {
        if (!isspace(curChar)) {
            value.append(1, curChar);
        }
    }
    startVal = atoi(value.c_str());
    value.erase();
    while (cmdFile.good() && (curChar = cmdFile.get()) != '<')
        ;
    if (cmdFile.peek() == '=') {
        plusOne = true;
    }
    while (cmdFile.good() && (curChar = cmdFile.get()) != ';') {
        if (!isspace(curChar)) {
            value.append(1, curChar);
        }
    }
    if (!plusOne) {
        endVal = atoi(value.c_str()) - 1;
    }
    else {
        endVal = atoi(value.c_str());
    }
    return;
}

void IfBlockProcess(string includeCmds, int threadNum) {
    unsigned int pos = 0, parenStack = 0;
    string condition, ifSentence, elseSentence;
    while (includeCmds[pos] != '(') {
        ++pos;
    }
    ++parenStack;
    ++pos;
    while (includeCmds[pos] != ')' || parenStack != 1) {
        condition.append(1, includeCmds[pos]);
        if (includeCmds[pos] == ')') {
            --parenStack;
        }
        else if (includeCmds[pos] == '(') {
            ++parenStack;
        }
        ++pos;
    }
    --parenStack;
    while (includeCmds[pos] != '{') {
        ++pos;
    }
    ++parenStack;
    ++pos;
    while (includeCmds[pos] != '}' || parenStack != 1) {
        ifSentence.append(1, includeCmds[pos]);
        if (includeCmds[pos] == '}') {
            --parenStack;
        }
        else if (includeCmds[pos] == '{') {
            ++parenStack;
        }
        ++pos;
    }
    --parenStack;
    while (pos < includeCmds.size() && isspace(includeCmds[pos])) {
        ++pos;
    }
    if (pos == includeCmds.size()) {
        elseSentence = "";
    }
    else if (includeCmds[pos] == 'e' && includeCmds[pos] == 'l') {
        while (includeCmds[pos] != '{') {
            ++pos;
        }
        ++parenStack;
        ++pos;
        while (includeCmds[pos] != '}' || parenStack != 1) {
            elseSentence.append(1, includeCmds[pos]);
            if (includeCmds[pos] == '}') {
                --parenStack;
            }
            else if (includeCmds[pos] == '{') {
                ++parenStack;
            }
            ++pos;
        }
        --parenStack;
    }
    bool status = isConditionTrue2(condition, threadNum);
    if (status) {
        istringstream iss(ifSentence);
        string token;
        vector<string> commands;
        while (getline(iss, token, ';')) {
            commands.push_back(token);
        }
        for (unsigned int i = 0; i < commands.size(); ++i) {
            ParseLine(commands[i], tupleSpace, threadNum);
        }
    }
    else {
        istringstream iss(elseSentence);
        string token;
        vector<string> commands;
        while (getline(iss, token, ';')) {
            commands.push_back(token);
        }
        for (unsigned int i = 0; i < commands.size(); ++i) {
            ParseLine(commands[i], tupleSpace, threadNum);
        }
    }
}

void* CmdProcess(void* tArgs) {
    ThreadArgs* args = static_cast<ThreadArgs*>(tArgs);
    string filePath = args->filePath;
    ifstream cmdFile(filePath.c_str());
    string line;
    if (!cmdFile.is_open()) {
        cerr << "error: can not open file " << filePath << endl;
        return NULL;
    }
    bool status = false, repeatFlag = false;
    char curChar; //max length of key words
    while (cmdFile.good()) {
        string curCmd;
        if (!repeatFlag) {
            curChar = cmdFile.get();
        }
        if (isspace(curChar))
            continue;
        curCmd.append(1, curChar);
        if (cmdFile.good()) {
            curCmd.append(1, static_cast<char>(cmdFile.get()));
        }
        if (curCmd == "ou" || curCmd == "in" || curCmd == "rd" || curCmd == "du") {
            while (cmdFile.good()) {
                char ch = cmdFile.get();
                if (ch == ';') {
                    break;
                }
                else {
                    curCmd.append(1, ch);
                }
            }
            if ("ou" == curCmd.substr(0, 2)) {
                ParseOutCmd(curCmd, tupleSpace, args->threadNum);
            }
            else if ("du" == curCmd.substr(0, 2)) {
                ParseDumpCmd(curCmd, tupleSpace);
            }
            else if ("inp" == curCmd.substr(0, 3)) {
                ParseInpCmd(curCmd, tupleSpace, args->threadNum, status);
            }
            else if ("rdp" == curCmd.substr(0, 3)) {
                ParseRdpCmd(curCmd, tupleSpace, args->threadNum, status);
            }
            else if ("in" == curCmd.substr(0, 2)) {
                ParseInCmd(curCmd, tupleSpace, args->threadNum);
            }
            else if ("rd" == curCmd.substr(0, 2)) {
                ParseRdCmd(curCmd, tupleSpace, args->threadNum);
            }
            else {
                cerr << "invalid input. failed and exit for thread " << args->threadNum;
                exit(-1);
            }
        }
        else if (curCmd == "if") {
            status = isConditionTrue(cmdFile, args->threadNum);
            if (status) {
                string includeCmds;
                while (cmdFile.good() && (curChar = cmdFile.get()) != '{') {
                    if (!isspace(curChar)) {
                        cerr << "invalid syntax. failed and exit.";
                        exit(-1);
                    }
                }
                if (!cmdFile.good()) {
                    cerr << "invalid syntax. failed and exit.";
                    exit(-1);
                }
                while (cmdFile.good() && (curChar = cmdFile.get()) != '}') {
                    includeCmds.append(1, curChar);
                }
                vector<string> lines;
                istringstream iss(includeCmds);
                string token;
                while (getline(iss, token, ';')) {
                    lines.push_back(token);
                }
                for (unsigned int i = 0; i < lines.size(); ++i) {
                    ParseLine(lines[i], tupleSpace, args->threadNum);
                }
                while (cmdFile.good()) {
                    curChar = cmdFile.get();
                    if (isspace(curChar)) {
                        continue;
                    }
                    else if (curChar != 'e') {
                        repeatFlag = true;
                        break;
                    }
                    else {
                        if (cmdFile.peek() == 'l') {
                            while (cmdFile.good() && (curChar = cmdFile.get()) != '}') {
                                ;
                            }
                            break;
                        }
                        else {
                            repeatFlag = true;
                            break;
                        }
                    }
                }
            }
            else {
                string includeCmds;
                while (cmdFile.good() && (curChar = cmdFile.get()) != '}') {
                    ;
                }
                while (cmdFile.good()) {
                    curChar = cmdFile.get();
                    if (isspace(curChar)) {
                        continue;
                    }
                    else if (curChar != 'e') {
                        repeatFlag = true;
                        break;
                    }
                    else {
                        if (cmdFile.peek() != 'l') {
                            repeatFlag = true;
                            break;
                        }
                        else {
                            while (cmdFile.good() && (curChar = cmdFile.get()) != '{') {
                                ;
                            }
                            while (cmdFile.good() && (curChar = cmdFile.get()) != '}') {
                                includeCmds.append(1, curChar);
                            }
                            vector<string> lines;
                            istringstream iss(includeCmds);
                            string token;
                            while (getline(iss, token, ';')) {
                                lines.push_back(token);
                            }
                            for (unsigned int i = 0; i < lines.size(); ++i) {
                                ParseLine(lines[i], tupleSpace, args->threadNum);
                            }
                        }
                    }
                }
            }
        }
        else if (curCmd == "fo") {
            string varName, includeCmds;
            int startVal, endVal, parenStack = 0;
            ParseForLoop(cmdFile, varName, startVal, endVal);
            while (cmdFile.good()) {
                curChar = cmdFile.get();
                if (curChar == '{') {
                    break;
                }
            }
            ++parenStack;
            while (cmdFile.good() && ((curChar = cmdFile.get()) != '}' || parenStack != 1)) {
                includeCmds.append(1, curChar);
                if (curChar == '}') {
                    --parenStack;
                }
                else if (curChar == '{') {
                    ++parenStack;
                }
            }
            for (int loop = startVal; loop <= endVal; ++loop) {
                string lines = includeCmds;
                do {
                    unsigned int pos = lines.find(varName);
                    if (!isalnum(lines[pos - 1]) &&
                            lines[pos - 1] != '_' &&
                            !isalnum(lines[pos + varName.size()]) &&
                            lines[pos + varName.size()] != '_') {
                        string tmp = lines.substr(0, pos);
                        tmp += to_string((long long int)loop);
                        tmp += lines.substr(pos + varName.size());
                        lines = tmp;
                    }
                } while (lines.find(varName) != string::npos);
                int checkPos = 0;
                while (isspace(lines[checkPos])) {
                    ++checkPos;
                }
                if (lines[checkPos] == 'i' && lines[checkPos + 1] == 'f') {
                    IfBlockProcess(lines, args->threadNum);
                }
                else {
                    vector<string> forBlockCmds;
                    istringstream iss(lines);
                    string token;
                    while (getline(iss, token, ';')) {
                        forBlockCmds.push_back(token);
                    }
                    for (unsigned int i = 0; i < forBlockCmds.size(); ++i) {
                        ParseLine(forBlockCmds[i], tupleSpace, args->threadNum);
                    }
                }
            }
        }
        else if (curCmd == "de") {
            string includeCmds;
            string funcName;
            int braceStack = 0;
            while (cmdFile.good() && (curChar = cmdFile.get()) != ' ')
                ;
            while (cmdFile.good() && (curChar = cmdFile.get()) != '{') {
                includeCmds.append(1, curChar);
            }
            includeCmds.append(1, curChar);
            ++braceStack;
            while (cmdFile.good() && (((curChar = cmdFile.get()) != '}') || braceStack != 1)) {
                includeCmds.append(1, curChar);
                if (curChar == '{') {
                    ++braceStack;
                }
                else if (curChar == '}') {
                    --braceStack;
                }
            }
            includeCmds.append(1, curChar);
            includeCmds.append(1, '\n');
            int funcNameEndPos = includeCmds.find('(');
            int funcNameStartPos = includeCmds.find_last_of(' ', funcNameEndPos);
            funcName = includeCmds.substr(funcNameStartPos + 1, funcNameEndPos - funcNameStartPos - 1);
            string fileName = "my_" + funcName + to_string((long long int)args->threadNum);
            funcSet[args->threadNum][funcName] = fileName;
            string fileNameCpp = fileName + ".cpp";
            string newCode = funcConstructor1 + includeCmds + funcConstructor2;
            int replacePos = newCode.find('@');
            newCode = newCode.substr(0, replacePos) + funcName + newCode.substr(replacePos + 1);
            ofstream output(fileNameCpp.c_str());
            output << newCode;
            output.close();
            string programCmd = "g++ -o " + fileName + " " + fileNameCpp;
            system(programCmd.c_str());
        }
    }
    cmdFile.close();
    return NULL;
}

int main() {
    string line;
    vector<string> files;
    while (getline(cin, line)) {
        files.push_back(line);
    }
    pthread_t threads[files.size()];
    ThreadArgs tArgs[files.size()];
    localVars.resize(files.size());
    funcSet.resize(files.size());
    int status;
    for (unsigned int i = 0; i < files.size(); ++i) {
        tArgs[i].threadNum = i;
        tArgs[i].filePath = files[i];
        status = pthread_create(&threads[i], NULL, CmdProcess, &tArgs[i]);
        if (status) {
            cerr << "pthread_create failed for thread " << i << "with error code " << status << endl;
        }
    }
    for (unsigned int i = 0; i < files.size(); ++i) {
        status = pthread_join(threads[i], NULL);
        if (status) {
            cerr << "pthread_join failed for thread " << i << "with error code " << status << endl;
        }
    }
    return 0;
}
