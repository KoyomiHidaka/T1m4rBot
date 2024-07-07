﻿#include <iostream>
#include <tgbot/tgbot.h>
#include <unordered_map>
#include <chrono>
#include <ctime>
#include <fstream>
#include <curl/curl.h>
#include <iomanip>
#include <boost/filesystem.hpp>
#pragma execution_character_set("utf-8")

bool inRunning = true;
using namespace TgBot;
using namespace std;
namespace fs = boost::filesystem;

const int64_t adminid = 1217311673; // Айди админа
//Данные по пользователю
chrono::time_point<chrono::system_clock> workStart;
chrono::time_point<chrono::system_clock> breakStart;
bool isWorking = false;
bool onBreak = false;
chrono::milliseconds totalWorkTime(0);

enum class State {
	START,
	FIRST_NAME,
	LAST_NAME,
	WORK_START,
	CONSUALTATION
};

struct UserInfo
{	
	int64_t userid;
	string firstname;
	string lastname;
};

struct UserWork {
	int userId;
	string documentFileId;
	string comment;
	string mark;
	string respond;
};
enum class AdminState {
	COMMENT,
	MARK,
	DONE,
	TROUBLE,
	RESPOND
};

unordered_map<int64_t, State> userStates;
unordered_map<int64_t, AdminState> adminStates;
unordered_map<int64_t, UserInfo> userInfo;
unordered_map<int64_t, bool> isHandlingState;
unordered_map<int64_t, bool> isHandlingAdminState;
unordered_map<int64_t, UserWork> userWorks;

void handleState(const Bot& bot, int64_t userId, Message::Ptr message) {
	InlineKeyboardMarkup::Ptr keyboard(new InlineKeyboardMarkup);
	InlineKeyboardButton::Ptr button1(new InlineKeyboardButton);
	
	switch (userStates[userId]) {
	case State::START:
		bot.getApi().sendMessage(userId, "Введите имя:");
		userStates[userId] = State::FIRST_NAME;
		isHandlingState[userId] = true;
		break;
	case State::FIRST_NAME:
		userInfo[userId].firstname = message->text;
		userInfo[userId].userid = userId;
		bot.getApi().sendMessage(userId, "Введите фамилию:");
		userStates[userId] = State::LAST_NAME;
		break;
	case State::LAST_NAME:
		userInfo[userId].lastname = message->text;
		bot.getApi().sendMessage(userId, "Регистрация завершена!");
		button1->text = "Начать";
		button1->callbackData = "start";
		keyboard->inlineKeyboard.push_back({ button1 });
		bot.getApi().sendMessage(message->chat->id, "Кнопка начать - начнет отчет проведенного времени работы\n Кнопка стоп - Приостановит время и начнет новый таймер отдыха", false, 0, keyboard);
		userStates[userId] = State::START;
		isHandlingState[userId] = false;
		break;
	case State::CONSUALTATION:
		bot.getApi().sendMessage(adminid, "Проблема от пользователя " + to_string(userId) + ": " + message->text, false, 0, keyboard);
		bot.getApi().sendMessage(userId, "Ваше сообщение отправлено в поддержку.");
		userStates[userId] = State::START;
		isHandlingState[userId] = false;
		break;
	}
}

string rewiev = " ";

void handleUserWork(const Bot& bot, int64_t userId, Message::Ptr message) {
	InlineKeyboardMarkup::Ptr keyboard(new InlineKeyboardMarkup);
	InlineKeyboardButton::Ptr button1(new InlineKeyboardButton);
	InlineKeyboardButton::Ptr button2(new InlineKeyboardButton);
	switch (adminStates[userId]) {
	case AdminState::COMMENT:
		bot.getApi().sendMessage(userId, "Введите комментарий:");
		adminStates[userId] = AdminState::MARK;
		break;
	case AdminState::MARK:
		userWorks[adminid].comment = message->text;
		bot.getApi().sendMessage(userId, "Введите оценку:");
		adminStates[userId] = AdminState::DONE;
		break;
	case AdminState::DONE:
		userWorks[adminid].mark = message->text;
		button1->text = "Отправить";
		button1->callbackData = "sendingReview";
		keyboard->inlineKeyboard.push_back({ button1 });
		bot.getApi().sendMessage(userId, "Данные сохранены. Отправить пользователю?", false, 0, keyboard);
		adminStates[userId] = AdminState::COMMENT;
		break;

	case AdminState::TROUBLE:
		bot.getApi().sendMessage(userId, "Сообщите решение проблемы:");
		adminStates[userId] = AdminState::RESPOND;
		break;
	case AdminState::RESPOND:
		userWorks[adminid].respond = message->text;
		button2->text = "Ответить";
		button2->callbackData = "sendRespond";
		keyboard->inlineKeyboard.push_back({ button2 });
		bot.getApi().sendMessage(userId, "Данные сохранены. Ответить пользователю?", false, 0, keyboard);
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
string formatWorkTime(chrono::milliseconds duration) {
	auto hours = chrono::duration_cast<std::chrono::hours>(duration).count();
	auto minutes = chrono::duration_cast<std::chrono::minutes>(duration % std::chrono::hours(1)).count();
	auto seconds = chrono::duration_cast<std::chrono::seconds>(duration % std::chrono::minutes(1)).count();
	auto milliseconds = duration.count() % 1000;
	return to_string(hours) + " ч " + to_string(minutes) + " мин " + to_string(seconds) + " сек";
	ostringstream oss;
	oss << std::setfill('0') << std::setw(2) << hours << ":"
		<< std::setfill('0') << std::setw(2) << minutes << ":"
		<< std::setfill('0') << std::setw(2) << seconds << "."
		<< std::setfill('0') << std::setw(3) << milliseconds;
	return oss.str();
}
void sendWorkingUsersList(Bot& bot, int64_t chatId) {
	ostringstream oss;
	oss << "Список работающих пользователей:\n";
	for (const auto& entry : isHandlingState) {
		int64_t userId = entry.first;
		if (isWorking) {
			auto now = chrono::system_clock::now();
			auto workTime = totalWorkTime + chrono::duration_cast<chrono::milliseconds>(now - workStart);
			oss << userInfo[userId].firstname << userInfo[userId].lastname << "User ID: " << userId << " - Время работы: " << formatWorkTime(workTime) << "\n";
		}
	}
	bot.getApi().sendMessage(chatId, oss.str());
}

atomic<bool> searching(false);
atomic<bool> src(false);
void performSearch(Bot& bot) {
	while (searching)
	{
		this_thread::sleep_for(chrono::seconds(1));
		totalWorkTime += chrono::seconds(1);
		if (!searching) {
			cout << "Stopping search" << endl;
			break;
		}
	}
}







//التحقق من مدة عمل البوت !!!!
void monitorTime(TgBot::Bot& bot, int64_t chatId) {
	while (src) {
		//this_thread::sleep_for(chrono::seconds(1));
		if (totalWorkTime >= chrono::hours(5)) {
			// Выводим сообщение о завершении рабочего дня
			bot.getApi().sendMessage(chatId, "Рабочий день завершается. Можно заканчивать работу или продолжить дальше.");
			src = false;  // Завершаем цикл поиска
		}
		if (!src) {
			cout << "Stopping counting" << endl;
			break;
		}

	}
}

int main()
{
	int64_t ussr;
	int availablebreak = 60;
	int startingi = 1;
	Bot bot("7203022991:AAHgQgzs7g0scjPS1zX2xAzL_ZQpTwsie5Q");
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
	/*ot.getEvents().onCommand("поддержка", [&bot](TgBot::Message::Ptr message) {
		int64_t ussr = message->from->id;

		cout << ussr;
		int64_t userId = message->from->id;
		userInfo[userId].userid = userId;
		bot.getApi().sendMessage(userId, "Пожалуйста, опишите вашу проблему:");
		userStates[userId] = State::CONSUALTATION;
		isHandlingState[userId] = true;
	});*/
	bot.getEvents().onCommand("work", [&bot](Message::Ptr message) {
		int64_t userId = message->from->id;
		if (userId == adminid) {
			adminStates[userId] = AdminState::COMMENT;
			handleUserWork(bot, userId, message);
		}
		else {
			bot.getApi().sendMessage(userId, "Вы не являетесь администратором.");
		}
	});
	/*bot.getEvents().onCommand("respond", [&bot](Message::Ptr message) {
		int64_t userId = message->from->id;
		if (userId == adminid) {
			adminStates[userId] = AdminState::TROUBLE;
			handleUserWork(bot, userId, message);
		}
		else {
			bot.getApi().sendMessage(userId, "Вы не являетесь администратором.");
		}
	});*/
	bot.getEvents().onAnyMessage([&bot](Message::Ptr message) {
		int64_t userId = message->from->id;

		if (adminStates.find(userId) != adminStates.end()) {
			handleUserWork(bot, userId, message);
		}
	});
	bot.getEvents().onCallbackQuery([&bot, &ussr](CallbackQuery::Ptr query) {
		int64_t adminId = query->from->id;
		if (query->data == "sendingReview") {

			string comment = userWorks[adminid].comment;
			string mark = userWorks[adminid].mark;
			try {
				bot.getApi().sendMessage(userInfo[adminid].userid, "Комментарий: " + comment + "\nОценка: " + mark);
				bot.getApi().sendMessage(adminId, "Оценка и комментарий отправлены пользователю.");
			}
			catch (TgException& e) {
				bot.getApi().sendMessage(adminId, "Ошибка при отправке сообщения пользователю: " + string(e.what()));
			}
		}
		//if (query->data == "sendRespond") {
		//	string respond = userWorks[adminid].respond;
		//	try {
		//		bot.getApi().sendMessage(userInfo[adminid].userid, "Решение проблемы: " + respond);
		//		bot.getApi().sendMessage(adminId, "Оценка и комментарий отправлены пользователю.");
		//	}
		//	catch (TgException& e) {
		//		bot.getApi().sendMessage(adminId, "Ошибка при отправке сообщения пользователю: " + string(e.what()));
		//	}
		//}
	});
		//Начало рабочего дня
	bot.getEvents().onCallbackQuery([&bot](CallbackQuery::Ptr query) {
		if (query->data == "start")
		{
			int64_t userId = query->message->chat->id;
			cout << userInfo[userId].userid;
			InlineKeyboardMarkup::Ptr keyboard(new InlineKeyboardMarkup);
			InlineKeyboardButton::Ptr button2(new InlineKeyboardButton);
			InlineKeyboardButton::Ptr button3(new InlineKeyboardButton);
			button2->text = "Взять перерыв";
			button2->callbackData = "break";
			keyboard->inlineKeyboard.push_back({ button2 });
			button3->text = "Закончить рабочий день";
			button3->callbackData = "end";
			keyboard->inlineKeyboard.push_back({ button3 });
			bot.getApi().sendMessage(query->message->chat->id, "Работа началась.", false, 0, keyboard);
			bot.getApi().answerCallbackQuery(query->id, " ", false);
			searching = true;
			thread searchThread(performSearch, ref(bot));
			searchThread.detach();

				// تشغيل موضوع للتحقق من الوقت
			thread monitorThread(monitorTime, ref(bot), query->message->chat->id);
			monitorThread.detach();

		}
	});


	bot.getEvents().onCommand("close", [&bot](Message::Ptr message) {
		int64_t userId = message->chat->id;
		if (message->from->id == adminid) {
			searching = false;
			bot.getApi().sendMessage(userInfo[userId].userid, "Работа остановлена администратором");
		}
	});

	bot.getEvents().onCallbackQuery([&bot](CallbackQuery::Ptr query) {
		if (query->data == "break")
		{
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
			//thread(startBreak, std::ref(bot), query->message->chat->id).detach();
		}
	});
	string messagdde = " ";
		//Перерыв
	bot.getEvents().onCallbackQuery([&bot, &availablebreak, &messagdde](CallbackQuery::Ptr query) {
		if (query->data == "minut10")
		{
			availablebreak -= 10;

			searching = false;

			string workTimeStr = formatWorkTime(totalWorkTime);
			messagdde = "Общее время работы: " + workTimeStr;
			bot.getApi().sendMessage(query->message->chat->id, messagdde);
			bot.getApi().answerCallbackQuery(query->id, "Перерыв окончится через 10 минут.", false);
			InlineKeyboardMarkup::Ptr keyboard(new InlineKeyboardMarkup);
			InlineKeyboardButton::Ptr button(new InlineKeyboardButton);
			button->text = "Продолжить работу";
			button->callbackData = "continue";
			keyboard->inlineKeyboard.push_back({ button });
			InlineKeyboardButton::Ptr button1(new InlineKeyboardButton);
			button1->text = "Закончить рабочий день";
			button1->callbackData = "end";
			keyboard->inlineKeyboard.push_back({ button1 });
			thread([&bot, chat_id = query->message->chat->id, keyboard]() {
				this_thread::sleep_for(chrono::minutes(1));
				bot.getApi().sendMessage(chat_id, "Перерыв закончился. Возобновите работу нажав Продолжить работу", false, 0, keyboard);
				onBreak = false;
			}).detach();
		}
		if (query->data == "minut20")
		{
			availablebreak -= 20;
			searching = false;

			InlineKeyboardMarkup::Ptr keyboard(new InlineKeyboardMarkup);
			InlineKeyboardButton::Ptr button(new InlineKeyboardButton);
			button->text = "Продолжить работу";
			button->callbackData = "continue";
			keyboard->inlineKeyboard.push_back({ button });
			InlineKeyboardButton::Ptr button1(new InlineKeyboardButton);
			button1->text = "Закончить рабочий день";
			button1->callbackData = "end";
			keyboard->inlineKeyboard.push_back({ button1 });
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
			searching = false;

			InlineKeyboardMarkup::Ptr keyboard(new InlineKeyboardMarkup);
			InlineKeyboardButton::Ptr button(new InlineKeyboardButton);
			button->text = "Продолжить работу";
			button->callbackData = "continue";
			keyboard->inlineKeyboard.push_back({ button });
			InlineKeyboardButton::Ptr button1(new InlineKeyboardButton);
			button1->text = "Закончить рабочий день";
			button1->callbackData = "end";
			keyboard->inlineKeyboard.push_back({ button1 });
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
				searching = false;

				InlineKeyboardMarkup::Ptr keyboard(new InlineKeyboardMarkup);
				InlineKeyboardButton::Ptr button(new InlineKeyboardButton);
				button->text = "Продолжить работу";
				button->callbackData = "continue";
				keyboard->inlineKeyboard.push_back({ button });
				InlineKeyboardButton::Ptr button1(new InlineKeyboardButton);
				button1->text = "Закончить рабочий день";
				button1->callbackData = "end";
				keyboard->inlineKeyboard.push_back({ button1 });
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
				searching = false;
				InlineKeyboardMarkup::Ptr keyboard(new InlineKeyboardMarkup);
				InlineKeyboardButton::Ptr button(new InlineKeyboardButton);
				button->text = "Продолжить работу";
				button->callbackData = "continue";
				keyboard->inlineKeyboard.push_back({ button });
				InlineKeyboardButton::Ptr button1(new InlineKeyboardButton);
				button1->text = "Закончить рабочий день";
				button1->callbackData = "end";
				keyboard->inlineKeyboard.push_back({ button1 });
				bot.getApi().sendMessage(query->message->chat->id, "Перерыв окончится через 50 минут.");
				bot.getApi().answerCallbackQuery(query->id, " ", false);
				thread([&bot, chat_id = query->message->chat->id, keyboard]() {
					this_thread::sleep_for(chrono::minutes(50));
					bot.getApi().sendMessage(chat_id, "Перерыв закончился. Возобновите работу нажав Продолжить работу", false, 0, keyboard);
					onBreak = false;
				}).detach();
			}
			if (query->data == "minut60")
			{
				availablebreak -= 60;

				searching = false;
				InlineKeyboardMarkup::Ptr keyboard(new InlineKeyboardMarkup);
				InlineKeyboardButton::Ptr button(new InlineKeyboardButton);
				button->text = "Продолжить работу";
				button->callbackData = "continue";
				keyboard->inlineKeyboard.push_back({ button });
				InlineKeyboardButton::Ptr button1(new InlineKeyboardButton);
				button1->text = "Закончить рабочий день";
				button1->callbackData = "end";
				keyboard->inlineKeyboard.push_back({ button1 });
				bot.getApi().sendMessage(query->message->chat->id, "Перерыв окончится через 60 минут.");
				bot.getApi().answerCallbackQuery(query->id, " ", false);
				thread([&bot, chat_id = query->message->chat->id, keyboard]() {
					this_thread::sleep_for(chrono::minutes(60));
					bot.getApi().sendMessage(chat_id, "Перерыв закончился. Возобновите работу нажав Продолжить работу", false, 0, keyboard);
					onBreak = false;
				}).detach();
			}
	});
	bot.getEvents().onCallbackQuery([&bot](CallbackQuery::Ptr query) {
		if (query->data == "continue")
		{

			searching = true;
			thread searchThread(performSearch, ref(bot));
			searchThread.detach();
			InlineKeyboardMarkup::Ptr keyboard(new InlineKeyboardMarkup);
			InlineKeyboardButton::Ptr button2(new InlineKeyboardButton);
			button2->text = "Взять перерыв";
			button2->callbackData = "break2nd";
			keyboard->inlineKeyboard.push_back({ button2 });
			InlineKeyboardButton::Ptr button1(new InlineKeyboardButton);
			button1->text = "Закончить рабочий день";
			button1->callbackData = "end";
			keyboard->inlineKeyboard.push_back({ button1 });
			bot.getApi().sendMessage(query->message->chat->id, "Работа началась.", false, 0, keyboard);
			bot.getApi().answerCallbackQuery(query->id, " ", false);
			auto fivehours = chrono::hours(5);
			if (totalWorkTime == fivehours) {
				bot.getApi().sendMessage(query->message->chat->id, "Норма на сегодняшний день выполнена, можешь продолжить работу или отправить на проверку");
			}
		}
	});
		//обработка перерыва
	bot.getEvents().onCallbackQuery([&bot, &availablebreak](CallbackQuery::Ptr query) {
		if ((query->data == "break2nd") && (availablebreak == 50))
		{
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
		}
		if ((query->data == "break2nd") && (availablebreak == 40))
		{
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
		}
		if ((query->data == "break2nd") && (availablebreak == 30))
		{
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
		}
		if ((query->data == "break2nd") && (availablebreak == 20))
		{
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
		}
		if ((query->data == "break2nd") && (availablebreak == 10))
		{
			InlineKeyboardMarkup::Ptr perestop(new InlineKeyboardMarkup);
			InlineKeyboardButton::Ptr pauz1(new InlineKeyboardButton);
			pauz1->text = "10 минут";
			pauz1->callbackData = "minut10";
			perestop->inlineKeyboard.push_back({ pauz1 });
			bot.getApi().sendMessage(query->message->chat->id, "Перерыв начался.", false, 0, perestop);
			bot.getApi().answerCallbackQuery(query->id, " ", false);
		}
		if ((query->data == "break2nd") && (availablebreak == 0))
		{
			bot.getApi().sendMessage(query->message->chat->id, "Доступное время для перерыва закончилось");
			InlineKeyboardMarkup::Ptr keyboard(new InlineKeyboardMarkup);
			InlineKeyboardButton::Ptr button1(new InlineKeyboardButton);
			button1->text = "Закончить рабочий день";
			button1->callbackData = "end";
			keyboard->inlineKeyboard.push_back({ button1 });
		}
	});


	bool acceptingFiles = false;


	bot.getEvents().onCallbackQuery([&bot, &acceptingFiles](CallbackQuery::Ptr query) {

		if (query->data == "end")
		{
			//searching = false;
			acceptingFiles = true;

			bot.getApi().sendMessage(query->message->chat->id, "Рабочий день окончен, отправьте файлы для оценки");
		}

	});





	bot.getEvents().onAnyMessage([&bot, &acceptingFiles, &messagdde](Message::Ptr message) {

		if ((acceptingFiles) && (message->document)) {
			string fileId = message->document->fileId;
			cout << "Received file ID: " << fileId << endl;
			File::Ptr file = bot.getApi().getFile(fileId);
			if (!file) {
				bot.getApi().sendMessage(message->chat->id, "Failed to get file info.");
				return;
			}

			string fileUrl = "https://api.telegram.org/file/bot" + bot.getToken() + "/" + file->filePath;
			cout << "File URL: " << fileUrl << std::endl;

			// Получаем имя файла из сообщения
			string fileName = message->document->fileName;

			int64_t userId = message->chat->id;
			string extension = fileName.substr(fileName.find_last_of('.') + 1);
			string name = userInfo[userId].firstname;

			int64_t id = message->chat->id;
			string idinStroke = to_string(id);
			string formatt = userInfo[userId].firstname + "--" + userInfo[userId].lastname + " ";

			// Путь к папке, где сохранятся файлы
			string localFolderPath = "incoming_files//";

			localFolderPath += formatt;


			// تنسيقات الفيديو
			if (extension == "cpp") {
				localFolderPath += "C++-files//";
				if (!fs::exists(localFolderPath)) {
					fs::create_directories(localFolderPath);
				}
			}
			if (extension == "py") {
				localFolderPath += "python-files//";
				if (!fs::exists(localFolderPath)) {
					fs::create_directories(localFolderPath);
				}
			}
			//تنسيقات النص
			if (extension == "docx") {
				localFolderPath += "docx-files//";
				if (!fs::exists(localFolderPath)) {
					fs::create_directories(localFolderPath);
				}
			}
			if (extension == "txt") {
				localFolderPath += "txt-files//";
				if (!fs::exists(localFolderPath)) {
					fs::create_directories(localFolderPath);
				}
			}
			if (extension == "mp4") {
				localFolderPath += "mp4-files//";
				if (!fs::exists(localFolderPath)) {
					fs::create_directories(localFolderPath);
				}
			}
			if (extension == "avi") {
				localFolderPath += "avi-files//";
				if (!fs::exists(localFolderPath)) {
					fs::create_directories(localFolderPath);
				}
			}
			if (extension == "mkv") {
				localFolderPath += "mkv-files//";
				if (!fs::exists(localFolderPath)) {
					fs::create_directories(localFolderPath);
				}
			}
			if (extension == "wmv") {
				localFolderPath += "wmv-files//";
				if (!fs::exists(localFolderPath)) {
					fs::create_directories(localFolderPath);
				}
			}
			if (extension == "plv") {
				localFolderPath += "plv-files//";
				if (!fs::exists(localFolderPath)) {
					fs::create_directories(localFolderPath);
				}
			}
			if (extension == "mov") {
				localFolderPath += "mov-files//";
				if (!fs::exists(localFolderPath)) {
					fs::create_directories(localFolderPath);
				}
			}
			if (extension == "webm") {
				localFolderPath += "webm-files//";
				if (!fs::exists(localFolderPath)) {
					fs::create_directories(localFolderPath);
				}
			}
			if (extension == "mpeg") {
				localFolderPath += "mpeg-files//";
				if (!fs::exists(localFolderPath)) {
					fs::create_directories(localFolderPath);
				}
			}
			if (extension == "3gp") {
				localFolderPath += "3gp-files//";
				if (!fs::exists(localFolderPath)) {
					fs::create_directories(localFolderPath);
				}
			}
			if (extension == "m4v") {
				localFolderPath += "m4v-files//";
				if (!fs::exists(localFolderPath)) {
					fs::create_directories(localFolderPath);
				}
			}
			if (extension == "mxf") {
				localFolderPath += "mxf-files//";
				if (!fs::exists(localFolderPath)) {
					fs::create_directories(localFolderPath);
				}
			}
			if (extension == "xavc") {
				localFolderPath += "xavc-files//";
				if (!fs::exists(localFolderPath)) {
					fs::create_directories(localFolderPath);
				}
			}
			if (extension == "pdf") {
				localFolderPath += "pdf-files//";
				if (!fs::exists(localFolderPath)) {
					fs::create_directories(localFolderPath);
				}
			}
			if (extension == "js") {
				localFolderPath += "javascript-files//";
				if (!fs::exists(localFolderPath)) {
					fs::create_directories(localFolderPath);
				}
			}
			//تنسيقات الصور
			if (extension == "jpg") {
				localFolderPath += "jpg-files//";
				if (!fs::exists(localFolderPath)) {
					fs::create_directories(localFolderPath);
				}
			}
			if (extension == "jpeg") {
				localFolderPath += "jpeg-files//";
				if (!fs::exists(localFolderPath)) {
					fs::create_directories(localFolderPath);
				}
			}
			if (extension == "png") {
				localFolderPath += "png-files//";
				if (!fs::exists(localFolderPath)) {
					fs::create_directories(localFolderPath);
				}
			}
			if (extension == "psd") {
				localFolderPath += "photoshop-files//";
				if (!fs::exists(localFolderPath)) {
					fs::create_directories(localFolderPath);
				}
			}
			if (extension == "ai") {
				localFolderPath += "Adobe-Illustrator-files//";
				if (!fs::exists(localFolderPath)) {
					fs::create_directories(localFolderPath);
				}
			}
			if (extension == "indd") {
				localFolderPath += "Adobe-InDesign Document-files//";
				if (!fs::exists(localFolderPath)) {
					fs::create_directories(localFolderPath);
				}
			}
			if (extension == "prproj") {
				localFolderPath += "Adobe-Premiere-Pro-Project-files//";
				if (!fs::exists(localFolderPath)) {
					fs::create_directories(localFolderPath);
				}
			}
			if (extension == "aep") {
				localFolderPath += "After-Effects-Project-files//";
				if (!fs::exists(localFolderPath)) {
					fs::create_directories(localFolderPath);
				}
			}
			if (extension == "sesx") {
				localFolderPath += "Adobe-Audition-Session-files//";
				if (!fs::exists(localFolderPath)) {
					fs::create_directories(localFolderPath);
				}
			}
			//تنسيقات الويب
			if (extension == "html") {
				localFolderPath += "HTML-files//";
				if (!fs::exists(localFolderPath)) {
					fs::create_directories(localFolderPath);
				}
			}
			if (extension == "css") {
				localFolderPath += "CSS-files//";
				if (!fs::exists(localFolderPath)) {
					fs::create_directories(localFolderPath);
				}
			}
			if (extension == "cs") {
				localFolderPath += "C#-files//";
				if (!fs::exists(localFolderPath)) {
					fs::create_directories(localFolderPath);
				}
			}
			if (extension == "java") {
				localFolderPath += "Java-files//";
				if (!fs::exists(localFolderPath)) {
					fs::create_directories(localFolderPath);
				}
			}

			/*else {
				cout << "Other File";
				localFolderPath += "others//";
				// Папка для других файлов
				if (!fs::exists(localFolderPath)) {
					fs::create_directories(localFolderPath);
				}
			}*/


			// Полный путь к сохраняемому файлу
			string localFilePath = localFolderPath + fileName;

			if (downloadFile(fileUrl, localFilePath)) {
				cout << "File downloaded successfully!";
				string workTimeStr = formatWorkTime(totalWorkTime);
				bot.getApi().sendMessage(adminid, "Пользователь " + userInfo[userId].firstname + " " + userInfo[userId].lastname + " отправил документ: " + fileName + "\n" + messagdde + "Общее время работы: " + workTimeStr);
				bot.getApi().sendDocument(adminid, fileId);




				bot.getApi().sendMessage(message->chat->id, "Ваша работа принята. Ожидайте оценки и комментария.");
				bot.getApi().sendMessage(userId, "Документ успешно отправлен администратору.");
			}
			else {
				cout << "Failed to download the file.";
				bot.getApi().sendMessage(userId, "Ошибка при загрузке документа.");
			}
		}
	});


	
	//команда стоп которая доступная только для администратора
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
	//команда список, показывается кто работает именно сейчас 
	bot.getEvents().onCommand("list", [&bot](TgBot::Message::Ptr message) {
		if (message->from->id == adminid) {
			sendWorkingUsersList(bot, message->chat->id);
		}
		else {
			bot.getApi().sendMessage(message->chat->id, "У вас нет прав для выполнения этой команды.");
		}
	});











	//запуск бота
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

