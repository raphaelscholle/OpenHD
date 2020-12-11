#include <cstdio>
#include <stdio.h>

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>

#include <iostream>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem.hpp>

#include <boost/regex.hpp>

#include "json.hpp"

#include "openhd-status.hpp"
#include "openhd-wifi.hpp"
#include "openhd-util.hpp"


#include "wifi.h"

WiFi::WiFi(boost::asio::io_service &io_service): m_io_service(io_service) {}


void WiFi::configure() {
    std::cout << "WiFi::configure()" << std::endl;

    process_manifest();

    for (auto card : m_wifi_cards) {
        process_card(card);
    }
}


void WiFi::process_manifest() {
    try {
        std::ifstream f("/tmp/wifi_manifest");
        nlohmann::json j;
        f >> j;

        m_wifi_hotspot_type = string_to_wifi_hotspot_type(j["hotspot"]);

        for (auto _card : j["cards"]) {

            WiFiCard card;
            card.name = _card["name"];

            card.type = string_to_wifi_card_type(_card["type"]);

            card.supports_5ghz = _card["supports_5ghz"];
            card.supports_2ghz = _card["supports_2ghz"];
            card.supports_injection = _card["supports_injection"];
            card.supports_hotspot = _card["supports_hotspot"];
            card.supports_rts = _card["supports_rts"];
            card.mac = _card["mac"];

            m_wifi_cards.push_back(card);
        }
    } catch (std::exception &ex) {
        // don't do anything, but send an error message to the user through the status service
        status_message(STATUS_LEVEL_EMERGENCY, "WiFi manifest processing failed");
        std::cerr << "WiFi::process_manifest: " << ex.what() << std::endl;
        return;
    }
}


void WiFi::process_card(WiFiCard card) {
    std::cerr << "Processing card: " << card.name << std::endl;

    /*
     * todo: this does not yet handle hotspot cards that also happen to support injection, for that
     *      we will have to let users mark one as being for hotspot use in the new settings system, once it's merged
     */
     if (!card.supports_injection) {
         std::cerr << "Card does not support injection: " << card.name << std::endl;
         setup_hotspot(card);
         return;
    }


    // todo: errors encountered here need to be submitted to the status service, users will never see stdout
    set_card_state(card, false);
    enable_monitor_mode(card);
    set_card_state(card, true);

    // todo: temporary hardcoding until new settings system is merged
    if (card.supports_5ghz) {
        set_frequency(card, m_default_5ghz_frequency);
    } else {
        set_frequency(card, m_default_2ghz_frequency);
    }

    // todo: temporary hardcoding until new settings system is merged    
    set_txpower(card, "3100");

    m_broadcast_cards.push_back(card);
}


/*
 *
 * todo: deduplicate this and similar logic in Ethernet/LTE, they should be subclasses of a common base
 */
void WiFi::setup_hotspot(WiFiCard card) {
    std::cout << "WiFi::setup_hotspot()" << std::endl;

    if (!card.supports_hotspot) {
        std::ostringstream message;
        message << "WiFi hotspot not supported on ";
        message << wifi_card_type_to_string(card.type);
        message << "cards (";
        message << card.name;
        message << ")";
        message << std::endl;
        status_message(STATUS_LEVEL_INFO, message.str());
    }

    bool success = false;

    if (m_hotspot_configured) {
        std::cout << "WiFi::setup_hotspot: already configured with another card" << std::endl;
        return;
    }

    // todo: allow the interface address to be configured. this requires changing the dnsmasq config file though, not
    //       just the interface address. 
    std::vector<std::string> args { card.name, m_wifi_hotspot_address, "up", };

    success = run_command("ifconfig", args);

    if (!success) {
        status_message(STATUS_LEVEL_WARNING, "Failed to enable wifi hotspot interface");
        return;
    }


    std::ostringstream message1;
    message1 << "WiFi hotspot enabled on frequency: ";

    std::string band;
    std::string channel;

    /*
     * Note: This is not currently choosing to use the band that wifibroadcast cards are not using. Most
     *       people seem to choose not to use the G band no matter what, so we might as well just default to 5Ghz
     *       and then perhaps lean on FEC to avoid interference, it's far more likely things will work that way
     *       than to prefer the 2.4ghz band in any situation.
     *
     */
    // todo: the channel, band, and txpower comes from the settings system once it's merged, not intended to be hardcoded
    if (card.supports_5ghz) {
        message1 << "Using band A, channel 165 for WiFi hotspot";
        channel = "165";
        band = "a";
    } else {
        message1 << "Using band G, channel 11 for WiFi hotspot";
        band = "g";
        channel = "11";
    }

    message1 << std::endl;
    status_message(STATUS_LEVEL_INFO, message1.str());

    {
        std::vector<std::string> args { 
            "/usr/local/share/wifibroadcast-scripts/wifi_hotspot.sh", band, channel, card.name, m_wifi_hotspot_txpower
        };

        success = run_command("/bin/bash", args);

        if (!success) {
            status_message(STATUS_LEVEL_WARNING, "Failed to enable hostap on wifi hotspot");
            return;
        }
    }
    
    m_hotspot_configured = true;
}


bool WiFi::set_card_name(WiFiCard card, std::string name) {
    std::cout << "WiFi::set_card_name()" << std::endl;

    std::vector<std::string> args { "link", "set", card.name, "name", name };

    bool success = run_command("ip", args);

    return success;
}


bool WiFi::set_card_state(WiFiCard card, bool up) {
    std::cout << "WiFi::set_card_state()" << std::endl;

    std::vector<std::string> args { "link", "set", "dev", card.name, up ? "up" : "down" };

    bool success = run_command("ip", args);

    return success;
}


bool WiFi::set_frequency(WiFiCard card, std::string frequency) {
    std::cout << "WiFi::set_frequency(" << frequency << ")" << std::endl;

    std::vector<std::string> args { "dev", card.name, "set", "freq", frequency };

    bool success = run_command("iw", args);

    return success;
}


bool WiFi::set_txpower(WiFiCard card, std::string txpower) {
    std::cout << "WiFi::set_txpower(" << txpower << ")" << std::endl;

    std::vector<std::string> args { "dev", card.name, "set", "txpower", "fixed", txpower };

    bool success = run_command("iw", args);

    return success;
}


bool WiFi::enable_monitor_mode(WiFiCard card) {
    std::cout << "WiFi::enable_monitor_mode()" << std::endl;

    std::vector<std::string> args { "dev", card.name, "set", "monitor", "otherbss" };

    bool success = run_command("iw", args);

    return success;
}