#include <iostream>
#include <tgbot/tgbot.h>
#include <unordered_map>
#include <chrono>
#include <ctime>
#include <fstream>
#include <curl/curl.h>
#include <iomanip>
#pragma execution_character_set("utf-8")

bool inRunning = true;
using namespace TgBot;
using namespace std;

const int64_t adminid = 869613280;
chrono::time_point<chrono::system_clock> workStart;
chrono::time_point<chrono::system_clock> breakStart;
bool isWorking = false;
bool onBreak = false;
chrono::milliseconds totalWorkTime(0);

enum class State {
	START,
	FIRST_NAME,
	LAST_NAME,
	WORK_START
};

struct UserInfo
{
	string firstname;
	string lastname;
};

unordered_map<int64_t, State> userStates;
unordered_map<int64_t, UserInfo> userInfo;
unordered_map<int64_t, bool> isHandlingState;

void handleState(const Bot& bot, int64_t userId, Message::Ptr message) {
	switch (userStates[userId]) {
	case State::START:
		bot.getApi().sendMessage(userId, "Введите имя:");
		userStates[userId] = State::FIRST_NAME;
		isHandlingState[userId] = true;
		break;
	case State::FIRST_NAME:
		userInfo[userId].firstname = message->text;
		bot.getApi().sendMessage(userId, "Введите фамилию:");
		userStates[userId] = State::LAST_NAME;
		break;
	case State::LAST_NAME:
		userInfo[userId].lastname = message->text;
		bot.getApi().sendMessage(userId, "Регистрация завершена!");
		InlineKeyboardMarkup::Ptr keyboard(new InlineKeyboardMarkup);
		InlineKeyboardButton::Ptr button1(new InlineKeyboardButton);
		button1->text = "Начать";
		button1->callbackData = "start";
		keyboard->inlineKeyboard.push_back({ button1 });
		bot.getApi().sendMessage(message->chat->id, "Кнопка начать - начнет отчет проведенного времени работы\n Кнопка стоп - Приостановит время и начнет новый таймер отдыха", false, 0, keyboard);
		userStates[userId] = State::START;
		isHandlingState[userId] = false;
		break;
	}
}



size_t WriteToFile(void* ptr, size_t size, size_t nmemb, FILE* stream) {
	size_t written = fwrite(ptr, size, nmemb, stream);
	return written;
}

bool downloadFile(const std::string& url, const std::string& outFile) {
	CURL* curl;
	FILE* fp;
	CURLcode res;
	curl = curl_easy_init();
	if (curl) {
		errno_t err = fopen_s(&fp, outFile.c_str(), "wb");
		if (err != 0) {
			curl_easy_cleanup(curl);
			return false;
		}
		curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToFile);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
		res = curl_easy_perform(curl);
		curl_easy_cleanup(curl);
		fclose(fp);
		return res == CURLE_OK;
	}
	return false;
}


void startBreak(Bot& bot, int64_t chatId) {
	this_thread::sleep_for(chrono::minutes(50));
	bot.getApi().sendMessage(chatId, "Перерыв окончится через 10 минут.");
	this_thread::sleep_for(chrono::minutes(10));
	bot.getApi().sendMessage(chatId, "Перерыв закончился. Возобновите работу командой 'Начать'.");
	onBreak = false;
}
std::string formatWorkTime(chrono::milliseconds duration) {
	auto hours = std::chrono::duration_cast<std::chrono::hours>(duration).count();
	auto minutes = std::chrono::duration_cast<std::chrono::minutes>(duration % std::chrono::hours(1)).count();
	auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration % std::chrono::minutes(1)).count();
	auto milliseconds = duration.count() % 1000;

	ostringstream oss;
	oss << std::setfill('0') << std::setw(2) << hours << ":"
		<< std::setfill('0') << std::setw(2) << minutes << ":"
		<< std::setfill('0') << std::setw(2) << seconds << "."
		<< std::setfill('0') << std::setw(3) << milliseconds;
	return oss.str();
}

int main()
{
	int availablebreak = 60;
	int startingi = 1;
	Bot bot("7365176474:AAEDErsurbtXg8k8_1s6amX0Przng-QSPKI");
	bot.getEvents().onCommand("start", [&bot](Message::Ptr message) {
		int64_t userId = message->from->id;
		userStates[userId] = State::START;
		handleState(bot, userId, message);
		});

	bot.getEvents().onAnyMessage([&bot](TgBot::Message::Ptr message) {
		int64_t userId = message->from->id;
		if (isHandlingState[userId]) {
			handleState(bot, userId, message);
			if (userStates[userId] == State::WORK_START) {
				isHandlingState[userId] = false; // Отключение обработчика после последнего состояния
			}
		}
		});
	int64_t lastUpdateId = 0;
	vector<Update::Ptr> updates = bot.getApi().getUpdates();
	for (const auto& update : updates) {
		if (update->updateId > lastUpdateId) {
			lastUpdateId = update->updateId;
		}
	}

	bot.getEvents().onCallbackQuery([&bot](CallbackQuery::Ptr query) {
		if (query->data == "start")
		{
			if (!isWorking && !onBreak) {
				workStart = std::chrono::system_clock::now();
				isWorking = true;
				InlineKeyboardMarkup::Ptr keyboard(new InlineKeyboardMarkup);
				InlineKeyboardButton::Ptr button2(new InlineKeyboardButton);
				button2->text = "Взять перерыв";
				button2->callbackData = "break";
				keyboard->inlineKeyboard.push_back({ button2 });
				bot.getApi().sendMessage(query->message->chat->id, "Работа началась.", false, 0, keyboard);
				bot.getApi().answerCallbackQuery(query->id, " ", false);
			}
		}
		});
	bot.getEvents().onCallbackQuery([&bot](CallbackQuery::Ptr query) {
		if (query->data == "break")
		{
			if (isWorking) {
				auto now = std::chrono::system_clock::now();
				totalWorkTime += std::chrono::duration_cast<std::chrono::milliseconds>(now - workStart);
				isWorking = false;
				onBreak = true;
				breakStart = now;
				InlineKeyboardMarkup::Ptr perestop(new InlineKeyboardMarkup);
				InlineKeyboardButton::Ptr pauz1(new InlineKeyboardButton);
				pauz1->text = "10 минут";
				pauz1->callbackData = "minut10";
				InlineKeyboardButton::Ptr pauz2(new InlineKeyboardButton);
				pauz2->text = "20 минут";
				pauz2->callbackData = "minut20";
				InlineKeyboardButton::Ptr pauz3(new InlineKeyboardButton);
				pauz3->text = "30 минут";
				pauz3->callbackData = "minut30";
				InlineKeyboardButton::Ptr pauz4(new InlineKeyboardButton);
				pauz4->text = "40 минут";
				pauz4->callbackData = "minut40";
				InlineKeyboardButton::Ptr pauz5(new InlineKeyboardButton);
				pauz5->text = "50 минут";
				pauz5->callbackData = "minut50";
				InlineKeyboardButton::Ptr pauz6(new InlineKeyboardButton);
				pauz6->text = "60 минут";
				pauz6->callbackData = "minut60";
				perestop->inlineKeyboard.push_back({ pauz1, pauz2, pauz3, pauz4, pauz5, pauz6 });
				bot.getApi().sendMessage(query->message->chat->id, "Перерыв начался.", false, 0, perestop);
				bot.getApi().answerCallbackQuery(query->id, " ", false);

				// Запускаем отдельный поток для отсчета времени перерыва
				thread(startBreak, std::ref(bot), query->message->chat->id).detach();
			}
		}
		});
	
	bot.getEvents().onCallbackQuery([&bot, &availablebreak](CallbackQuery::Ptr query) {
		if (query->data == "minut10")
		{
			availablebreak -= 10;
			auto now = std::chrono::system_clock::now();
			totalWorkTime += std::chrono::duration_cast<std::chrono::milliseconds>(now - workStart);
			isWorking = false;
			onBreak = true;
			breakStart = now;
			
			std::string workTimeStr = formatWorkTime(totalWorkTime);
			std::string messagdde = "Общее время работы: " + workTimeStr;

			bot.getApi().sendMessage(query->message->chat->id, messagdde);
			bot.getApi().answerCallbackQuery(query->id, "Перерыв окончится через 10 минут.", false);
			InlineKeyboardMarkup::Ptr keyboard(new InlineKeyboardMarkup);
			InlineKeyboardButton::Ptr button(new InlineKeyboardButton);
			button->text = "Продолжить работу";
			button->callbackData = "continue";
			keyboard->inlineKeyboard.push_back({ button });
			thread([&bot, chat_id = query->message->chat->id, keyboard]() {
				this_thread::sleep_for(chrono::minutes(1));
				bot.getApi().sendMessage(chat_id, "Перерыв закончился. Возобновите работу нажав Продолжить работу", false, 0, keyboard);
				onBreak = false;
			}).detach();
			
		}
		if (query->data == "minut20")
		{
			availablebreak -= 20;
			auto now = std::chrono::system_clock::now();
			totalWorkTime += std::chrono::duration_cast<std::chrono::milliseconds>(now - workStart);
			isWorking = false;
			onBreak = true;
			breakStart = now;
			InlineKeyboardMarkup::Ptr keyboard(new InlineKeyboardMarkup);
			InlineKeyboardButton::Ptr button(new InlineKeyboardButton);
			button->text = "Продолжить работу";
			button->callbackData = "continue";
			keyboard->inlineKeyboard.push_back({ button });
			bot.getApi().sendMessage(query->message->chat->id, "Перерыв окончится через 20 минут.");
			bot.getApi().answerCallbackQuery(query->id, " ", false);

			thread([&bot, chat_id = query->message->chat->id, keyboard]() {
				this_thread::sleep_for(chrono::minutes(20));
				bot.getApi().sendMessage(chat_id, "Перерыв закончился. Возобновите работу нажав Продолжить работу", false, 0, keyboard);
				onBreak = false;
			}).detach();
		}
		if (query->data == "minut30")
		{
			availablebreak -= 30;
			auto now = std::chrono::system_clock::now();
			totalWorkTime += std::chrono::duration_cast<std::chrono::milliseconds>(now - workStart);
			isWorking = false;
			onBreak = true;
			breakStart = now;
			InlineKeyboardMarkup::Ptr keyboard(new InlineKeyboardMarkup);
			InlineKeyboardButton::Ptr button(new InlineKeyboardButton);
			button->text = "Продолжить работу";
			button->callbackData = "continue";
			keyboard->inlineKeyboard.push_back({ button });
			bot.getApi().sendMessage(query->message->chat->id, "Перерыв окончится через 30 минут.");
			bot.getApi().answerCallbackQuery(query->id, " ", false);
			thread([&bot, chat_id = query->message->chat->id, keyboard]() {
				this_thread::sleep_for(chrono::minutes(30));
				bot.getApi().sendMessage(chat_id, "Перерыв закончился. Возобновите работу нажав Продолжить работу", false, 0, keyboard);
				onBreak = false;
			}).detach();
		}
		if (query->data == "minut40")
		{
			availablebreak -= 40;
			auto now = std::chrono::system_clock::now();
			totalWorkTime += std::chrono::duration_cast<std::chrono::milliseconds>(now - workStart);
			isWorking = false;
			onBreak = true;
			breakStart = now;
			InlineKeyboardMarkup::Ptr keyboard(new InlineKeyboardMarkup);
			InlineKeyboardButton::Ptr button(new InlineKeyboardButton);
			button->text = "Продолжить работу";
			button->callbackData = "continue";
			keyboard->inlineKeyboard.push_back({ button });
			bot.getApi().sendMessage(query->message->chat->id, "Перерыв окончится через 40 минут.");
			bot.getApi().answerCallbackQuery(query->id, " ", false);
			thread([&bot, chat_id = query->message->chat->id, keyboard]() {
				this_thread::sleep_for(chrono::minutes(40));
				bot.getApi().sendMessage(chat_id, "Перерыв закончился. Возобновите работу нажав Продолжить работу", false, 0, keyboard);
				onBreak = false;
			}).detach();
		}
		if (query->data == "minut50")
		{
			availablebreak -= 50;
			auto now = std::chrono::system_clock::now();
			totalWorkTime += std::chrono::duration_cast<std::chrono::milliseconds>(now - workStart);
			isWorking = false;
			onBreak = true;
			breakStart = now;
			InlineKeyboardMarkup::Ptr keyboard(new InlineKeyboardMarkup);
			InlineKeyboardButton::Ptr button(new InlineKeyboardButton);
			button->text = "Продолжить работу";
			button->callbackData = "continue";
			keyboard->inlineKeyboard.push_back({ button });
			bot.getApi().sendMessage(query->message->chat->id, "Перерыв окончится через 50 минут.");
			bot.getApi().answerCallbackQuery(query->id, " ", false);
			thread([&bot, chat_id = query->message->chat->id, keyboard]() {
				this_thread::sleep_for(chrono::minutes(20));
				bot.getApi().sendMessage(chat_id, "Перерыв закончился. Возобновите работу нажав Продолжить работу", false, 0, keyboard);
				onBreak = false;
			}).detach();
		}
		if (query->data == "minut60")
		{
			availablebreak -= 60;
			auto now = chrono::system_clock::now();
			totalWorkTime += chrono::duration_cast<chrono::milliseconds>(now - workStart);
			isWorking = false;
			onBreak = true;
			breakStart = now;
			InlineKeyboardMarkup::Ptr keyboard(new InlineKeyboardMarkup);
			InlineKeyboardButton::Ptr button(new InlineKeyboardButton);
			button->text = "Продолжить работу";
			button->callbackData = "continue";
			keyboard->inlineKeyboard.push_back({ button });
			bot.getApi().sendMessage(query->message->chat->id, "Перерыв окончится через 60 минут.");
			bot.getApi().answerCallbackQuery(query->id, " ", false);
			thread([&bot, chat_id = query->message->chat->id, keyboard]() {
				this_thread::sleep_for(chrono::minutes(20));
				bot.getApi().sendMessage(chat_id, "Перерыв закончился. Возобновите работу нажав Продолжить работу", false, 0, keyboard);
				onBreak = false;
			}).detach();
		}
	});

	bot.getEvents().onCallbackQuery([&bot](CallbackQuery::Ptr query) {
		if (query->data == "continue")
		{
			workStart = chrono::system_clock::now();
			InlineKeyboardMarkup::Ptr keyboard(new InlineKeyboardMarkup);
			InlineKeyboardButton::Ptr button2(new InlineKeyboardButton);
			button2->text = "Взять перерыв";
			button2->callbackData = "break2nd";
			keyboard->inlineKeyboard.push_back({ button2 });
			bot.getApi().sendMessage(query->message->chat->id, "Работа началась.", false, 0, keyboard);
			bot.getApi().answerCallbackQuery(query->id, " ", false);
		}
	});

	bot.getEvents().onCallbackQuery([&bot, &availablebreak](CallbackQuery::Ptr query) {
		if ((query->data == "break2nd") && (availablebreak == 50))
		{
			auto now = std::chrono::system_clock::now();
			totalWorkTime += std::chrono::duration_cast<std::chrono::milliseconds>(now - workStart);
			isWorking = false;
			onBreak = true;
			breakStart = now;
			InlineKeyboardMarkup::Ptr perestop(new InlineKeyboardMarkup);
			InlineKeyboardButton::Ptr pauz1(new InlineKeyboardButton);
			pauz1->text = "10 минут";
			pauz1->callbackData = "minut10";
			InlineKeyboardButton::Ptr pauz2(new InlineKeyboardButton);
			pauz2->text = "20 минут";
			pauz2->callbackData = "minut20";
			InlineKeyboardButton::Ptr pauz3(new InlineKeyboardButton);
			pauz3->text = "30 минут";
			pauz3->callbackData = "minut30";
			InlineKeyboardButton::Ptr pauz4(new InlineKeyboardButton);
			pauz4->text = "40 минут";
			pauz4->callbackData = "minut40";
			InlineKeyboardButton::Ptr pauz5(new InlineKeyboardButton);
			pauz5->text = "50 минут";
			pauz5->callbackData = "minut50";
			perestop->inlineKeyboard.push_back({ pauz1, pauz2, pauz3, pauz4, pauz5 });
			bot.getApi().sendMessage(query->message->chat->id, "Перерыв начался.", false, 0, perestop);
			bot.getApi().answerCallbackQuery(query->id, " ", false);

			// Запускаем отдельный поток для отсчета времени перерыва
			//thread(startBreak, std::ref(bot), query->message->chat->id).detach();
		}
		if ((query->data == "break2nd") && (availablebreak == 40))
		{
			auto now = std::chrono::system_clock::now();
			totalWorkTime += std::chrono::duration_cast<std::chrono::milliseconds>(now - workStart);
			isWorking = false;
			onBreak = true;
			breakStart = now;
			InlineKeyboardMarkup::Ptr perestop(new InlineKeyboardMarkup);
			InlineKeyboardButton::Ptr pauz1(new InlineKeyboardButton);
			pauz1->text = "10 минут";
			pauz1->callbackData = "minut10";
			InlineKeyboardButton::Ptr pauz2(new InlineKeyboardButton);
			pauz2->text = "20 минут";
			pauz2->callbackData = "minut20";
			InlineKeyboardButton::Ptr pauz3(new InlineKeyboardButton);
			pauz3->text = "30 минут";
			pauz3->callbackData = "minut30";
			InlineKeyboardButton::Ptr pauz4(new InlineKeyboardButton);
			pauz4->text = "40 минут";
			pauz4->callbackData = "minut40";
			perestop->inlineKeyboard.push_back({ pauz1, pauz2, pauz3, pauz4 });
			bot.getApi().sendMessage(query->message->chat->id, "Перерыв начался.", false, 0, perestop);
			bot.getApi().answerCallbackQuery(query->id, " ", false);

			// Запускаем отдельный поток для отсчета времени перерыва
			//thread(startBreak, std::ref(bot), query->message->chat->id).detach();
		}
		if ((query->data == "break2nd") && (availablebreak == 30))
		{
			auto now = std::chrono::system_clock::now();
			totalWorkTime += std::chrono::duration_cast<std::chrono::milliseconds>(now - workStart);
			isWorking = false;
			onBreak = true;
			breakStart = now;
			InlineKeyboardMarkup::Ptr perestop(new InlineKeyboardMarkup);
			InlineKeyboardButton::Ptr pauz1(new InlineKeyboardButton);
			pauz1->text = "10 минут";
			pauz1->callbackData = "minut10";
			InlineKeyboardButton::Ptr pauz2(new InlineKeyboardButton);
			pauz2->text = "20 минут";
			pauz2->callbackData = "minut20";
			InlineKeyboardButton::Ptr pauz3(new InlineKeyboardButton);
			pauz3->text = "30 минут";
			pauz3->callbackData = "minut30";
			perestop->inlineKeyboard.push_back({ pauz1, pauz2, pauz3 });
			bot.getApi().sendMessage(query->message->chat->id, "Перерыв начался.", false, 0, perestop);
			bot.getApi().answerCallbackQuery(query->id, " ", false);

			// Запускаем отдельный поток для отсчета времени перерыва
			//thread(startBreak, std::ref(bot), query->message->chat->id).detach();
		}
		if ((query->data == "break2nd") && (availablebreak == 20))
		{
			auto now = std::chrono::system_clock::now();
			totalWorkTime += std::chrono::duration_cast<std::chrono::milliseconds>(now - workStart);
			isWorking = false;
			onBreak = true;
			breakStart = now;
			InlineKeyboardMarkup::Ptr perestop(new InlineKeyboardMarkup);
			InlineKeyboardButton::Ptr pauz1(new InlineKeyboardButton);
			pauz1->text = "10 минут";
			pauz1->callbackData = "minut10";
			InlineKeyboardButton::Ptr pauz2(new InlineKeyboardButton);
			pauz2->text = "20 минут";
			pauz2->callbackData = "minut20";
			perestop->inlineKeyboard.push_back({ pauz1, pauz2 });
			bot.getApi().sendMessage(query->message->chat->id, "Перерыв начался.", false, 0, perestop);
			bot.getApi().answerCallbackQuery(query->id, " ", false);

			// Запускаем отдельный поток для отсчета времени перерыва
			//thread(startBreak, std::ref(bot), query->message->chat->id).detach();
		}
		if ((query->data == "break2nd") && (availablebreak == 10))
		{
			auto now = std::chrono::system_clock::now();
			totalWorkTime += std::chrono::duration_cast<std::chrono::milliseconds>(now - workStart);
			isWorking = false;
			onBreak = true;
			breakStart = now;
			InlineKeyboardMarkup::Ptr perestop(new InlineKeyboardMarkup);
			InlineKeyboardButton::Ptr pauz1(new InlineKeyboardButton);
			pauz1->text = "10 минут";
			pauz1->callbackData = "minut10";
			perestop->inlineKeyboard.push_back({ pauz1 });
			bot.getApi().sendMessage(query->message->chat->id, "Перерыв начался.", false, 0, perestop);
			bot.getApi().answerCallbackQuery(query->id, " ", false);

			// Запускаем отдельный поток для отсчета времени перерыва
			//thread(startBreak, std::ref(bot), query->message->chat->id).detach();
		}
		if ((query->data == "break2nd") && (availablebreak == 0))
		{
			bot.getApi().sendMessage(query->message->chat->id, "Доступное время для перерыва закончилось");
		}
		});

	bot.getEvents().onCommand("send", [&bot](Message::Ptr message) {
		bot.getApi().sendMessage(message->chat->id, "Send Document");
		bot.getEvents().onAnyMessage([&bot](TgBot::Message::Ptr message) {
			if (message->document) {
				string fileId = message->document->fileId;
				cout << "Received file ID: " << fileId << std::endl;

				TgBot::File::Ptr file = bot.getApi().getFile(fileId);
				if (!file) {
					bot.getApi().sendMessage(message->chat->id, "Failed to get file info.");
					return;
				}

				string fileUrl = "https://api.telegram.org/file/bot" + bot.getToken() + "/" + file->filePath;
				cout << "File URL: " << fileUrl << std::endl;

				// Получаем имя файла из сообщения
				string fileName = message->document->fileName;

				// Путь к папке, где вы хотите сохранять файлы
				string localFolderPath = "C://Users//overs//source//repos//testnewfunctiof//testnewfunctiof//p[ps//";

				// Полный путь к сохраняемому файлу
				string localFilePath = localFolderPath + fileName;

				if (downloadFile(fileUrl, localFilePath)) {
					bot.getApi().sendMessage(message->chat->id, "File downloaded successfully!");
				}
				else {
					bot.getApi().sendMessage(message->chat->id, "Failed to download the file.");
				}
			}
			});
		});

	bot.getEvents().onCommand("stop", [&bot, &startingi](Message::Ptr message) {
		if (message->from->id == adminid) {
			bot.getApi().sendMessage(message->chat->id, "Программа приостановила свою работу...!");
			system("exit");
			startingi = 2;
		}
		else {
			bot.getApi().sendMessage(message->chat->id, "У вас недостаточно прав для совершения данной операции");
		}
	});















	try {
		printf("Bot username: %s\n", bot.getApi().getMe()->username.c_str());
		TgLongPoll longPoll(bot, 60, lastUpdateId + 1);
		while (inRunning) {
			auto updates = bot.getApi().getUpdates(lastUpdateId + 1);
			for (auto& update : updates) {
				lastUpdateId = update->updateId;
			}
			if (startingi == 2) {
				break;
			}
			printf("Long poll started\n");
			longPoll.start();
		}
	}
	catch (TgException& e) {
		printf("error: %s\n", e.what());
	}
	return 0;
}










// ЗАВТРА ДОБАВИТЬ это отсчет времени при нажатии на кнопку завершить работу

/*auto fiveHours = std::chrono::hours(5);
if (totalWorkTime < fiveHours) {
	message += "\nРаботайте еще";
}*/











