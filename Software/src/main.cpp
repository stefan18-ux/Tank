#include <atomic>
#include <string>
#include <vector>
#include <format>
#include <esp_timer.h>
#include <LiquidCrystal.h>
#include <AccelStepper.h>

#include "defs.hpp"

std::atomic<float> distance{-1.0f};
float last_recorded_distance{-1.0f};

class Player {
public:
	static constexpr uint8_t max_level = 9;
	static constexpr uint8_t min_level = 3;

    explicit Player(uint8_t pin = BUZZER) : pin(pin) {}

    void begin() {
        ledcAttach(pin, 2000, res_bits);
        silence();

        const esp_timer_create_args_t args = {
            .callback = &Player::on_timer,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "buzzer",
        };
        esp_timer_create(&args, &timer);
    }

    void play() {
        if (active) return;
        active = true;
        index = 0;
        start_note();
    }

    void stop() {
        active = false;
        silence();
    }

    bool playing() const { return active; }

    void set(uint16_t distance) {
		if (distance > max_dist)
			distance = max_dist;
		if (distance < min_dist)
			distance = min_dist;
		
		float volume = (max_level - min_level) * (distance - min_dist) / (max_dist - min_dist) + min_level;
		
		volume = max_level - volume + min_level;

		level = volume;

		apply(); 
	}

private:
    struct Note { uint16_t freq; uint16_t ms; };

    static constexpr Note song[] = {
        {1800, 60}, {0, 40}, {1800, 60}, {0, 250},
    };

    static constexpr size_t song_len = sizeof(song) / sizeof(song[0]);

    static constexpr uint8_t res_bits  = 10;

    static void on_timer(void* self) {                 // called when a note ends
        auto* p = static_cast<Player*>(self);
        
		if (!p->active)
			return;
        
		p->index = (p->index + 1) % song_len;
        p->start_note();
    }

    void start_note() {
        if (song[index].freq) {
            ledcChangeFrequency(pin, song[index].freq, res_bits);
            ledcWrite(pin, 1u << level);
        } else {
            silence();
        }

        esp_timer_start_once(timer, song[index].ms * 1000ULL);   // µs until next note
    }

    void apply() {
        if (active && song[index].freq) 
			ledcWrite(pin, 1u << level);
    }

    void silence() { 
		ledcWrite(pin, 0); 
	}

	static constexpr uint16_t max_dist = 30;
	static constexpr uint16_t min_dist = 5;

    uint8_t pin;
    uint8_t level{min_level};
    bool active = false;
    size_t index = 0;
    esp_timer_handle_t timer = nullptr;
};

Player player;

class Display {
public:
	enum class DisplayState: uint8_t {Menu, Calibration, Rotation, Shoot};

	Display():
		lcd(RS, EN, D4, D5, D6, D7),
		should_blink{true},
		blink_state{BlinkState::Blink},
		display_state{DisplayState::Menu},
		display_buf {{
			displays::menu, 
			displays::calibration,
			displays::rotation,
			displays::shoot
		}},
		modified_state{true},
		curr_rotor_speed{lcd_col_capacity / 2},
		curr_index{std::vector<uint8_t>(display_buf.size())}
	{
		// rotation speed
		display_buf[2][0] = std::move(std::string(lcd_col_capacity, '-'));
		display_buf[2][0][curr_rotor_speed + 1] = '\0';
		lcd.begin(lcd_col_capacity, lcd_line_capacity);
	}

	DisplayState state() const {
		return display_state;
	}

	void print() {
		uint8_t curr_state = static_cast<uint8_t>(display_state);
		uint8_t show_from = curr_index[curr_state];
		uint8_t show_to = std::min(static_cast<uint8_t>(show_from + lcd_line_capacity),
								static_cast<uint8_t>(display_buf[curr_state].size()));
		
		if (!modified_state)
		{
			if (should_blink) {
				lcd.setCursor(0, 0);
				do_blink(curr_state);
			}

			return;
		}

		display_buf[3][0] = std::format("Distance: {:.1f}", distance.load());
		
		lcd.clear();
		blink_state = BlinkState::Blink;
		
		for (uint8_t i = show_from; i < show_to; i++) { 
			lcd.setCursor(0, i - show_from);
			if (i == show_from && should_blink)
				do_blink(curr_state);
			else
				lcd.print(display_buf[curr_state][i].data());
		}
		
		modified_state = false;
	}

	void iterate(int8_t position) {
		if (display_state != DisplayState::Menu)
			return;

		uint8_t curr_state = static_cast<uint8_t>(display_state);

		if (!curr_index[curr_state] && position == UP)
			curr_index[curr_state] = display_buf[curr_state].size() - 1;
		else if (curr_index[curr_state] == display_buf[curr_state].size() - 1 && position == DOWN)
			curr_index[curr_state] = 0;
		else
			curr_index[curr_state] += position;
		
		modified_state = true;
	}

	void select() {
		// when in cursor interactive modes, press is for selecting
		// when in non cursor interactive modes, press is for going back
		uint8_t curr_state = static_cast<uint8_t>(display_state);

		switch(display_state) {
			case DisplayState::Menu: {
				should_blink = false;
				switch (curr_index[curr_state]) {
					case 0:
						display_state = DisplayState::Calibration;
						player.stop();
						break;
					case 1:
						display_state = DisplayState::Rotation;
						player.stop();
						break;
					default:
						display_state = DisplayState::Shoot;
						player.play();
						break;
				}

				break;
			}
			default: {
				should_blink = true;
				display_state = DisplayState::Menu;
				player.stop();
				break;
			}
		}

		modified_state = true;
	}

	void modify_rotor_speed(int8_t position) {
		if (curr_rotor_speed == 1 && position < 0)
			return;
		if (curr_rotor_speed == lcd_col_capacity && position > 0)
			return;

		uint8_t curr_state = static_cast<uint8_t>(display_state);
		
		display_buf[curr_state][0][curr_rotor_speed + 1] = '-';

		curr_rotor_speed += position;
		modified_state = true;

		display_buf[curr_state][0][curr_rotor_speed + 1] = '\0';
	}

	uint8_t get_speed() const {
		return curr_rotor_speed;
	}

	uint8_t get_max_speed() const {
		return lcd_col_capacity;
	}
private:
	static constexpr uint8_t lcd_line_capacity = 2;
	static constexpr uint8_t lcd_col_capacity = 16;
	static constexpr uint16_t blink_after = 800;

	bool should_blink;

	DisplayState display_state;
	enum class BlinkState: uint8_t {Blink, Line} blink_state;

	std::vector<std::vector<std::string>> display_buf;
	std::vector<uint8_t> curr_index;
	std::string rotation_speed;
	
	uint8_t curr_rotor_speed;
	bool modified_state;

	LiquidCrystal lcd;

	void do_blink(uint8_t curr_state) {
		if (millis() % blink_after >= (5 * blink_after) / 6 && blink_state == BlinkState::Line) {
			lcd.print(displays::empty_line.data());
			blink_state = BlinkState::Blink;
		}
		else if (millis() % blink_after < (5 * blink_after) / 6 && blink_state == BlinkState::Blink) {
			lcd.print(display_buf[curr_state][curr_index[curr_state]].data());
			blink_state = BlinkState::Line;
		}
	}

	friend void sensor_task(void*);
};

Display display;

class Joystick {
public:
	Joystick():
		px(JOY_X), py(JOY_Y), psw(JOY_SW),
		read_at{0}
	{}

	enum class Input: uint8_t {Up, Down, Left, Right, None};

	Input read() {
		size_t curr_time = millis();

		if (curr_time < read_at)
			return Input::None;
		
		read_at = curr_time + read_after;

		uint16_t read_x = analogRead(px);
		uint16_t read_y = analogRead(py);

		bool x_movement = !(read_x >= median_reading_val - reading_thresh &&
			read_x <= median_reading_val + reading_thresh);
		bool y_movement = !(read_y >= median_reading_val - reading_thresh &&
			read_y <= median_reading_val + reading_thresh);
		
		if (!x_movement && !y_movement)
			return Input::None;

		if (read_x > median_reading_val + reading_thresh)
			return Input::Right;
		
		if (read_x < median_reading_val - reading_thresh)
			return Input::Left;

		if (read_y > median_reading_val + reading_thresh)
			return Input::Down;
		
		if (read_y < median_reading_val - reading_thresh)
			return Input::Up;

		return Input::None;
	}

	int16_t x_offset() const {
    	return static_cast<int>(analogRead(px)) - median_reading_val;
	}
	
private:
	static constexpr uint16_t read_after{150};
	static constexpr uint16_t reading_thresh{400};
	static constexpr uint16_t median_reading_val{4095/2};
	
	uint8_t px, py, psw;

	size_t read_at;
};

Joystick joystick;

class Executor {
public:
	Executor():
		motor (AccelStepper::HALF4WIRE,
		STEPPER_IN1, STEPPER_IN3, STEPPER_IN2, STEPPER_IN4)
	{
		// start the motor with half a speed
		motor.setMaxSpeed(max_speed / 2);
        motor.setAcceleration(max_acceleration);
	}

	void execute(int8_t position) {
		if (display.state() == Display::DisplayState::Rotation)
			display.modify_rotor_speed(position);
	}

	void update(int16_t movement) {
		switch (display.state()) {
			case Display::DisplayState::Calibration:
				turn(movement, 0);
				break;
			case Display::DisplayState::Shoot:
			    turn(movement, limit_steps);
				break;
			default:
				motor.setMaxSpeed(max_speed);
				motor.moveTo(0);
				break;
		}
		motor.run();
	}

	void set() {
		if (display.state() == Display::DisplayState::Calibration)
			motor.setCurrentPosition(0);
	}
private:
 	static constexpr float min_speed{80.0};
	static constexpr float max_speed{800.0};
	static constexpr float max_acceleration{1500.0};

	static constexpr float angle_limit{80.0};
	static constexpr uint16_t deadzone{300};
	static constexpr long full_range{4096}; 
    static constexpr long limit_steps = full_range * angle_limit / 360;
	
	AccelStepper motor;

	void turn(int16_t joy_x, long limit) {
		if (abs(joy_x) < deadzone) {
			motor.stop();
			return;
		}

		float n = std::min(1.0f, (abs(joy_x) - deadzone) / float((full_range / 2) - deadzone));

		uint16_t curr_set_speed = display.get_speed();
		uint16_t max_possible_speed = display.get_max_speed();
		
		float top = std::max(curr_set_speed * max_speed / max_possible_speed,
							static_cast<float>(min_speed));

		motor.setMaxSpeed(min_speed + n * (top - min_speed));

		long end = limit ? limit : 1000000;
		motor.moveTo(joy_x > 0 ? end : -end);
	}
};

Executor executor;

void on_press() {
	static size_t next_bounce;
	static uint8_t min_diff{150};
	
	size_t curr_time = millis();

	if (curr_time < next_bounce)
		return;
	next_bounce = curr_time + min_diff;

	executor.set();
	display.select();
}

void sensor_task(void*) {
    for (;;) {
        digitalWrite(TRIGGER, LOW);
        delayMicroseconds(2);
        digitalWrite(TRIGGER, HIGH);
        delayMicroseconds(10);
        digitalWrite(TRIGGER, LOW);

        unsigned long us = pulseIn(ECO, HIGH, 30000);
        distance = us ? us / 58.0f : -1.0f;

		if (distance != last_recorded_distance)
			display.modified_state = true;

		player.set(distance.load());

		last_recorded_distance = distance;

        vTaskDelay(pdMS_TO_TICKS(60));
    }
}

void setup() {
    Serial.begin(115200);

	pinMode(JOY_SW, INPUT_PULLUP);
	attachInterrupt(digitalPinToInterrupt(JOY_SW), on_press, FALLING);

	pinMode(TRIGGER, OUTPUT);
    pinMode(ECO, INPUT);

	xTaskCreatePinnedToCore(
        sensor_task,
        "sonar",
        4096,
        nullptr,
        1,
        nullptr,
        1
    );

	player.begin();
}

void loop() {
	Joystick::Input input = joystick.read();

	switch(input) {
		case Joystick::Input::Down:
			display.iterate(DOWN);
			break;
		case Joystick::Input::Left:
			executor.execute(LEFT);
			break;
		case Joystick::Input::Right:
			executor.execute(RIGHT);
			break;
		case Joystick::Input::Up:
			display.iterate(UP);
			break;
		default:
			break;
	}

	executor.update(joystick.x_offset());
	display.print();
}