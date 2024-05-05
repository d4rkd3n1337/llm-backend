#include "db.hpp"
#include <openssl/sha.h>


pgdb::pgdb() : conn(std::make_unique<pqxx::connection>(getDBDetails()))
{
    try {
        if (!this->conn->is_open()) {
            std::cout << "Failed to connect to PostgreSQL." << std::endl;
        }
        // if (this->conn->is_open()) {
        //     std::cout << "Connected to PostgreSQL successfully." << std::endl;
        // } else {
        //     std::cout << "Failed to connect to PostgreSQL." << std::endl;
        // }

    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

pgdb::~pgdb()
{
    conn->disconnect();
    // std::cout << "Disconnected from PostgreSQL." << std::endl;
}

std::string pgdb::hashPassword(const std::string &username, const std::string &password)
{
    std::string saltedPassword = username.substr(0, username.length() / 2) + password;
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, saltedPassword.c_str(), saltedPassword.length());
    SHA256_Final(hash, &sha256);

    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
}

bool pgdb::createUser(const std::string &username, const std::string &pwd)
{
    try {

        if(this->usernameExists(username) != req_state::not_found)
            return false;

        this->conn->prepare("create_user", R"(
            INSERT INTO "users" (username, password)
            VALUES ($1, $2)
        )");

        // Calculate the salt using the first half of the username
        std::string salt = username.substr(0, username.length() / 2);

        // Hash the password using the salt
        std::string hashedPassword = hashPassword(pwd, salt);

        pqxx::work txn(*this->conn);
        txn.exec_prepared("create_user", username, hashedPassword);
        txn.commit();

        std::cout << "User created successfully!" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    return false;
}

pgdb::req_state pgdb::usernameExists(const std::string &username)
{
    try {
        pqxx::work txn(*this->conn);

        // Prepare the SQL statement
        conn->prepare("check_username", "SELECT COUNT(*) FROM \"users\" WHERE username = $1");

        // Execute the prepared statement
        pqxx::result res = txn.exec_prepared("check_username", username);

        // Return true if the count is greater than 0, false otherwise
        return (res[0][0].as<int>() > 0) ? req_state::found : req_state::not_found;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    return req_state::error;
}

bool pgdb::auth(std::string &username, std::string &password)
{
    try {
        pqxx::work txn(*conn);

        // Calculate the salt using the first half of the username
        std::string salt = username.substr(0, username.length() / 2);

        // Hash the provided password using the salt
        std::string hashedPassword = hashPassword(password, salt).substr(0, 31);

        printf("%s\n", hashedPassword.c_str());

        // Prepare the SQL statement
        conn->prepare("authenticate_user", "SELECT COUNT(*) FROM \"users\" WHERE username = $1 AND password = $2");

        // Execute the prepared statement
        pqxx::result res = txn.exec_prepared("authenticate_user", username, hashedPassword);

        // Return true if the count is greater than 0, false otherwise
        return res[0][0].as<int>() > 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return false;
    }
}

bool pgdb::auth(std::string &token)
{
    try {
        pqxx::work txn(*this->conn);

        // Prepare the SQL statement
        conn->prepare("check_token", "SELECT COUNT(*) FROM \"tokens\" WHERE token = $1");

        // Execute the prepared statement
        pqxx::result res = txn.exec_prepared("check_token", token);

        // Return true if the count is greater than 0, false otherwise
        return (res[0][0].as<int>() > 0);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return false;
    }
}

int pgdb::userIDFromToken(std::string &token)
{
    try {
        pqxx::work txn(*this->conn);

        // Prepare the SQL statement
        conn->prepare("userid_token", "SELECT user_id FROM \"tokens\" WHERE token = $1");

        // Execute the prepared statement
        pqxx::result res = txn.exec_prepared("userid_token", token);

        // Return true if the count is greater than 0, false otherwise
        return res[0][0].as<int>();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return -1;
    }
}

void pgdb::saveToken(const std::string &username, const std::string &token)
{
    try {
        pqxx::work txn(*conn);
        // Find the user_id for the given username
        conn->prepare("find_user_id", "SELECT id FROM users WHERE username = $1");
        pqxx::result res = txn.exec_prepared("find_user_id", username);

        if (res.empty()) {
            std::cerr << "Error: User not found." << std::endl;
            return;
        }

        int user_id = res[0][0].as<int>();

        // Insert the token into the tokens table
        conn->prepare("insert_token", "INSERT INTO tokens (token, user_id, created_at) VALUES ($1, $2, CURRENT_TIMESTAMP)");
        txn.exec_prepared("insert_token", token, user_id);
        txn.commit();

        std::cout << "Token saved successfully!" << std::endl;      
        return;  
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return;
    }
}

int pgdb::createChat(int user1Id, int user2Id)
{
    try {
        pqxx::work txn(*conn);

        conn->prepare("create_chat", "INSERT INTO chats (user1_id, user2_id) VALUES ($1, $2)");
        txn.exec_prepared("create_chat", user1Id, user2Id);

        conn->prepare("get_last_chat_id", "SELECT id FROM chats ORDER BY id DESC LIMIT 1");
        pqxx::result res = txn.exec_prepared("get_last_chat_id");
        txn.commit();
        return res[0][0].as<int>();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return -1;
    }
}

void pgdb::storeMessage(int chatId, int senderId, const std::string &message)
{
    try {
        pqxx::work txn(*conn);
        conn->prepare("store_message", "INSERT INTO messages (chat_id, sender_id, message) VALUES ($1, $2, $3)");
        txn.exec_prepared("store_message", chatId, senderId, message);
        txn.commit();
        std::cout << "Message stored successfully!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

std::vector<std::string> pgdb::retrieveMessages(int ownerId, int chatId)
{
    std::vector<std::string> messages;
    try {
        pqxx::work txn(*conn);
        conn->prepare("retrieve_messages", "SELECT * FROM messages WHERE chat_id = $1 and sender_id = $2 ORDER BY timestamp");
        pqxx::result res = txn.exec_prepared("retrieve_messages", chatId, ownerId);
        for (const pqxx::row& row : res) {
            messages.push_back(row[2].as<std::string>());
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    return messages;
}

std::vector<std::pair<std::string, bool>> pgdb::getChatMessages(int chatId, int userId)
{
    std::vector<std::pair<std::string, bool>> messages;
    try {
        pqxx::work txn(*conn);
        conn->prepare("retrieve_messages", "SELECT message, sender_id FROM messages WHERE chat_id = $1 ORDER BY timestamp");
        pqxx::result res = txn.exec_prepared("retrieve_messages", chatId);
        for (const pqxx::row& row : res) {
            std::string message = row[0].as<std::string>();
            int senderId = row[1].as<int>();
            bool isLocal = (senderId == userId);
            messages.emplace_back(message, isLocal);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    return messages;
}

std::vector<int> pgdb::getChatIdsForUser(int userId)
{
    std::vector<int> chatIds;
    try {
        pqxx::work txn(*conn);
        conn->prepare("get_chat_ids_for_user", "SELECT id FROM chats WHERE user1_id = $1 OR user2_id = $1");
        pqxx::result res = txn.exec_prepared("get_chat_ids_for_user", userId);
        for (const pqxx::row& row : res) {
            chatIds.push_back(row[0].as<int>());
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    return chatIds;
}

std::string pgdb::getFirstMessageOfChat(int chatId)
{
    std::string message;
    try {
        pqxx::work txn(*conn);
        conn->prepare("get_first_message", "SELECT message FROM messages WHERE chat_id = $1 ORDER BY timestamp LIMIT 1");
        pqxx::result res = txn.exec_prepared("get_first_message", chatId);
        if (!res.empty()) {
            message = res[0][0].as<std::string>();
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    return message;
}

void pgdb::removeChatsForUser(int userId)
{
    try {
        pqxx::work txn(*conn);

        // Prepare the SQL statement
        conn->prepare("remove_chats_for_user", "DELETE FROM chats WHERE user1_id = $1 OR user2_id = $1");

        // Execute the prepared statement
        txn.exec_prepared("remove_chats_for_user", userId);
        txn.commit();

        std::cout << "Chats removed successfully for user " << userId << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

void pgdb::removeChatById(int chatId)
{
    try {
        pqxx::work txn(*conn);

        // Prepare the SQL statement to remove the chat
        // Prepare the SQL statement to remove the messages
        conn->prepare("remove_messages_by_chat_id", "DELETE FROM messages WHERE chat_id = $1");

        // Remove the associated messages
        txn.exec_prepared("remove_messages_by_chat_id", chatId);

        // Prepare the SQL statement to remove the chat
        conn->prepare("remove_chat_by_id", "DELETE FROM chats WHERE id = $1");

        // Remove the chat
        txn.exec_prepared("remove_chat_by_id", chatId);
        txn.commit();

        std::cout << "Chat and associated messages removed successfully with ID: " << chatId << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

void pgdb::saveDoc(int userId, const std::string& docName, const std::string& docContent) {
    try {
        pqxx::work txn(*conn);
        conn->prepare("insert_doc", "INSERT INTO docs (user_id, doc_name, doc_content) VALUES ($1, $2, $3)");
        txn.exec_prepared("insert_doc", userId, docName, docContent);
        txn.commit();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

std::string pgdb::getDoc(int userId, const std::string& docName) {
    try {
        pqxx::work txn(*conn);
        conn->prepare("get_doc", "SELECT doc_content FROM docs WHERE user_id = $1 AND doc_name = $2");
        pqxx::result res = txn.exec_prepared("get_doc", userId, docName);
        return res.empty() ? "" : res[0]["doc_content"].as<std::string>();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return "";
    }
}

std::vector<std::string> pgdb::getUserDocs(int userId) {
    try {
        pqxx::work txn(*conn);
        conn->prepare("get_user_docs", "SELECT doc_name FROM docs WHERE user_id = $1");
        pqxx::result res = txn.exec_prepared("get_user_docs", userId);
        std::vector<std::string> docNames;
        for (const auto& row : res) {
            docNames.push_back(row["doc_name"].as<std::string>());
        }
        return docNames;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return {};
    }
}

void pgdb::deleteDoc(int userId, const std::string& docName) {
    try {
        pqxx::work txn(*conn);
        conn->prepare("delete_doc", "DELETE FROM docs WHERE user_id = $1 AND doc_name = $2");
        txn.exec_prepared("delete_doc", userId, docName);
        txn.commit();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

void pgdb::deleteAllUserDocs(int userId) {
    try {
        pqxx::work txn(*conn);
        conn->prepare("delete_all_user_docs", "DELETE FROM docs WHERE user_id = $1");
        txn.exec_prepared("delete_all_user_docs", userId);
        txn.commit();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}