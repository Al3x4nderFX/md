#include <Geode/Geode.hpp>
#include <Geode/modify/EditorUI.hpp>

using namespace geode::prelude;

// ---------------------------------------------------------------
// Nodo de la barra: dos filas (normal = verde, práctica = azul)
// ---------------------------------------------------------------
class EditorProgressBar : public CCNode {
protected:
    LevelEditorLayer* m_lel = nullptr;
    CCLayerColor* m_normalFill = nullptr;
    CCLayerColor* m_practiceFill = nullptr;
    CCLabelBMFont* m_label = nullptr;

    std::string m_key;
    float m_normal = 0.f;
    float m_practice = 0.f;
    bool m_wasPlaying = false;
    bool m_dirty = false;

    static constexpr float W = 180.f;
    static constexpr float H = 5.f;

    static std::string makeKey(GJGameLevel* level) {
        return fmt::format("{}_{}", level->m_levelID.value(), std::string(level->m_levelName));
    }

    bool init(LevelEditorLayer* lel) {
        if (!CCNode::init()) return false;
        m_lel = lel;
        m_key = makeKey(lel->m_level);

        // Cargar lo guardado
        auto mod = Mod::get();
        m_normal = static_cast<float>(mod->getSavedValue<int>(m_key + "_n", 0));
        m_practice = static_cast<float>(mod->getSavedValue<int>(m_key + "_p", 0));

        this->setContentSize({W, H * 2 + 4.f});
        this->setAnchorPoint({0.5f, 0.5f});

        auto bg = CCLayerColor::create({0, 0, 0, 140}, W + 4.f, H * 2 + 6.f);
        bg->setPosition({-2.f, -3.f});
        this->addChild(bg, 0);

        auto normalBack = CCLayerColor::create({60, 60, 60, 200}, W, H);
        normalBack->setPosition({0.f, H + 1.f});
        this->addChild(normalBack, 1);

        auto practiceBack = CCLayerColor::create({60, 60, 60, 200}, W, H);
        practiceBack->setPosition({0.f, 0.f});
        this->addChild(practiceBack, 1);

        m_normalFill = CCLayerColor::create({0, 255, 0, 255}, 0.f, H);
        m_normalFill->setPosition({0.f, H + 1.f});
        this->addChild(m_normalFill, 2);

        m_practiceFill = CCLayerColor::create({0, 200, 255, 255}, 0.f, H);
        m_practiceFill->setPosition({0.f, 0.f});
        this->addChild(m_practiceFill, 2);

        m_label = CCLabelBMFont::create("", "bigFont.fnt");
        m_label->setScale(0.28f);
        m_label->setAnchorPoint({0.f, 0.5f});
        m_label->setPosition({W + 6.f, H + 0.5f});
        this->addChild(m_label, 3);

        this->refresh();
        this->schedule(schedule_selector(EditorProgressBar::tick));
        return true;
    }

    void refresh() {
        m_normalFill->setContentSize({W * m_normal / 100.f, H});
        m_practiceFill->setContentSize({W * m_practice / 100.f, H});
        m_label->setVisible(Mod::get()->getSettingValue<bool>("show-text"));
        m_label->setString(
            fmt::format("{}% / {}%", static_cast<int>(m_normal), static_cast<int>(m_practice)).c_str()
        );
    }

    void save() {
        auto mod = Mod::get();
        mod->setSavedValue<int>(m_key + "_n", static_cast<int>(m_normal));
        mod->setSavedValue<int>(m_key + "_p", static_cast<int>(m_practice));
        m_dirty = false;
    }

    void tick(float) {
        if (!m_lel) return;

        bool playing = m_lel->m_playbackMode == PlaybackMode::Playing;

        if (playing && m_lel->m_player1) {
            float len = m_lel->getLastObjectX();
            if (len < 1.f) len = 1.f;
            float pct = std::clamp(m_lel->m_player1->getPositionX() / len * 100.f, 0.f, 100.f);

            float& target = m_lel->m_isPracticeMode ? m_practice : m_normal;
            if (pct > target) {
                target = pct;
                m_dirty = true;
                this->refresh();
            }
        }

        // Al salir del playtest, guardar
        if (m_wasPlaying && !playing && m_dirty) this->save();
        m_wasPlaying = playing;
    }

public:
    static EditorProgressBar* create(LevelEditorLayer* lel) {
        auto ret = new EditorProgressBar();
        if (ret->init(lel)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    void onExit() override {
        if (m_dirty) this->save();
        CCNode::onExit();
    }
};

// ---------------------------------------------------------------
// Hook: añadir la barra al editor
// ---------------------------------------------------------------
class $modify(ProgressEditorUI, EditorUI) {
    bool init(LevelEditorLayer* lel) {
        if (!EditorUI::init(lel)) return false;

        auto winSize = CCDirector::get()->getWinSize();
        auto bar = EditorProgressBar::create(lel);
        if (bar) {
            bar->setID("progress-bar"_spr);
            bar->setScale(
                static_cast<float>(Mod::get()->getSettingValue<double>("bar-scale"))
            );
            bar->setPosition({winSize.width / 2.f - 20.f, winSize.height - 16.f});
            this->addChild(bar, 100);
        }
        return true;
    }
};
